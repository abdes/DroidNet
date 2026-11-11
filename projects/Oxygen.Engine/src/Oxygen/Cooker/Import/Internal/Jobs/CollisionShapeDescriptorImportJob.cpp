//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/HashedBytes.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/CollisionShapeDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/JoltShapeCook.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedMeshSurface.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  namespace phys = data::pak::physics;
  using internal::ShapePart;

  struct DescriptorTarget final {
    std::string relpath;
    std::string virtual_path;
  };

  //! The diagnostic codes of a reference that fails to resolve.
  struct ReferenceCodes final {
    std::string_view unresolved;
    std::string_view wrong_type;
  };

  //! The cooked backend payload of one part, before it is emitted.
  struct PartPayload final {
    phys::ShapePayloadType type = phys::ShapePayloadType::kInvalid;
    std::vector<std::byte> bytes;
  };

  //! A part whose payload failed to cook; the session holds the diagnostic.
  struct PartCookFailed final { };

  //! A part's payload; nothing for an analytic part.
  using PartCook = std::expected<std::optional<PartPayload>, PartCookFailed>;

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const std::string_view code, std::string message,
    std::string json_pointer = {}) -> void
  {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = std::string(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = std::move(json_pointer),
    });
  }

  //! Places the descriptor at the job's identity.
  [[nodiscard]] auto ResolveDescriptorTarget(const ImportRequest& request,
    ImportSession& session) -> std::optional<DescriptorTarget>
  {
    auto placement
      = internal::PlaceAsset(request, data::kCollisionShapeDescriptorExtension);
    if (!placement.has_value()) {
      session.AddDiagnostic(std::move(placement.error()));
      return std::nullopt;
    }
    return DescriptorTarget {
      .relpath = std::move(placement->descriptor_relpath),
      .virtual_path = std::move(placement->virtual_path),
    };
  }

  //! Looks up the asset `text` names in any mounted cooked root; the session
  //! records what it requires of another mount when it finalizes.
  [[nodiscard]] auto LookupAsset(ImportSession& session,
    const ImportRequest& request,
    const std::span<const internal::MountedCookedSource> mounts,
    const std::string_view text, const std::string& json_pointer,
    const data::AssetType expected_type, const ReferenceCodes& codes)
    -> std::optional<data::AssetKey>
  {
    const auto reference = internal::ResolveJobReference(request, text);
    if (!reference.has_value()) {
      session.AddDiagnostic(internal::RefuseReference(
        request, text, reference.error(), json_pointer));
      return std::nullopt;
    }
    const auto found
      = internal::LookupReference(mounts, request, *reference, expected_type);
    if (found.has_value()) {
      return found->key;
    }
    const auto& refused = found.error();
    switch (refused.reason) {
    case internal::AssetReferenceRefusal::kUnmounted:
      session.AddDiagnostic(
        internal::RefuseUnmountedReference(request, *reference, json_pointer));
      break;
    case internal::AssetReferenceRefusal::kMissing:
      AddDiagnostic(session, request, codes.unresolved,
        "Reference was not found: "
          + internal::DescribeLookup(*reference, refused),
        json_pointer);
      break;
    case internal::AssetReferenceRefusal::kTypeMismatch:
      AddDiagnostic(session, request, codes.wrong_type,
        "Reference names a "
          + std::string(data::to_string(
            refused.found_type.value_or(data::AssetType::kUnknown)))
          + " asset, expected a " + std::string(data::to_string(expected_type))
          + ": " + internal::DescribeLookup(*reference, refused),
        json_pointer);
      break;
    }
    return std::nullopt;
  }

  //! Reads the triangle surface of the geometry a part references.
  [[nodiscard]] auto ReadPartSurface(ImportSession& session,
    const ImportRequest& request,
    const std::span<const internal::MountedCookedSource> mounts,
    const std::string& geometry_ref, const std::string& json_pointer)
    -> std::optional<internal::CookedMeshSurface>
  {
    const auto key = LookupAsset(session, request, mounts, geometry_ref,
      json_pointer, data::AssetType::kGeometry,
      ReferenceCodes {
        .unresolved = import_codes::kPhysicsShapeGeometryRefUnresolved,
        .wrong_type = import_codes::kPhysicsShapeGeometryRefNotGeometry,
      });
    if (!key.has_value()) {
      return std::nullopt;
    }
    const auto* const content = internal::FindListingContent(mounts, *key);
    CHECK_NOTNULL_F(content, "a found geometry is listed by a mounted root");
    auto surface = internal::ReadCookedMeshSurface(*content, *key);
    if (!surface.has_value()) {
      AddDiagnostic(session, request,
        import_codes::kPhysicsShapeGeometryUnreadable,
        "'" + geometry_ref + "' gives no triangle surface: " + surface.error(),
        json_pointer);
      return std::nullopt;
    }
    return std::move(*surface);
  }

  //! Cooks the backend payload of a part that has one: a convex hull, a
  //! triangle mesh or a height field. Nothing for an analytic part.
  [[nodiscard]] auto CookPartPayload(ImportSession& session,
    const ImportRequest& request,
    const std::span<const internal::MountedCookedSource> mounts,
    const ShapePart& part) -> PartCook
  {
    using Cooked = std::expected<std::vector<std::byte>, std::string>;
    auto type = phys::ShapePayloadType::kInvalid;
    auto cooked = std::optional<Cooked> {};
    auto unreadable = false;
    std::visit(
      [&](const auto& params) -> void {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, internal::ConvexHullParams>
          || std::is_same_v<T, internal::TriangleMeshParams>) {
          const auto surface = ReadPartSurface(session, request, mounts,
            params.geometry_ref, part.json_pointer + "/geometry_ref");
          if (!surface.has_value()) {
            unreadable = true;
            return;
          }
          if constexpr (std::is_same_v<T, internal::ConvexHullParams>) {
            type = phys::ShapePayloadType::kConvex;
            cooked = internal::CookJoltConvexHull(params, *surface);
          } else {
            type = phys::ShapePayloadType::kMesh;
            cooked = internal::CookJoltTriangleMesh(*surface);
          }
        } else if constexpr (std::is_same_v<T, internal::HeightFieldParams>) {
          type = phys::ShapePayloadType::kHeightField;
          cooked = internal::CookJoltHeightField(params);
        }
      },
      part.params);
    if (unreadable) {
      return std::unexpected(PartCookFailed {});
    }
    if (!cooked.has_value()) {
      return std::optional<PartPayload> {};
    }
    if (!cooked->has_value()) {
      AddDiagnostic(session, request, import_codes::kPhysicsShapeCookFailed,
        "Jolt refused the shape: " + cooked->error(), part.json_pointer);
      return std::unexpected(PartCookFailed {});
    }
    return std::optional<PartPayload> {
      PartPayload { .type = type, .bytes = std::move(**cooked) },
    };
  }

  //! Emits a cooked part payload; its resource key, or nothing on failure.
  [[nodiscard]] auto EmitPartPayload(ImportSession& session,
    const ImportRequest& request, PartPayload payload,
    const std::string& json_pointer) -> std::optional<data::AssetKey>
  {
    auto cooked = CookedPhysicsResourcePayload {};
    cooked.data = HashedBytes(std::move(payload.bytes));
    cooked.format = phys::PhysicsResourceFormat::kJoltShapeBinary;
    cooked.alignment = 16;
    try {
      return session.PhysicsResourceEmitter()
        .Emit(std::move(cooked))
        .resource_asset_key;
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request,
        import_codes::kPhysicsShapePayloadEmitFailed,
        "Failed emitting the shape payload: " + std::string(ex.what()),
        json_pointer);
      return std::nullopt;
    }
  }

  //! Writes the analytic parameters of `params` into the cooked arm `out`.
  auto WriteShapeParams(const internal::ShapeParams& params,
    const phys::ShapeType type, phys::ShapeParams& out) -> void
  {
    std::visit(
      [&](const auto& p) -> void {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, internal::SphereParams>) {
          out.sphere.radius = p.radius;
        } else if constexpr (std::is_same_v<T, internal::AxialParams>) {
          if (type == phys::ShapeType::kCapsule) {
            out.capsule.radius = p.radius;
            out.capsule.half_height = p.half_height;
          } else if (type == phys::ShapeType::kCylinder) {
            out.cylinder.radius = p.radius;
            out.cylinder.half_height = p.half_height;
          } else {
            out.cone.radius = p.radius;
            out.cone.half_height = p.half_height;
          }
        } else if constexpr (std::is_same_v<T, internal::BoxParams>) {
          std::ranges::copy(p.half_extents, std::begin(out.box.half_extents));
        } else if constexpr (std::is_same_v<T, internal::PlaneParams>) {
          std::ranges::copy(p.normal, std::begin(out.plane.normal));
          out.plane.distance = p.distance;
        } else if constexpr (std::is_same_v<T, internal::WorldBoundaryParams>) {
          out.world_boundary.boundary_mode = p.mode;
          std::ranges::copy(
            p.limits_min, std::begin(out.world_boundary.limits_min));
          std::ranges::copy(
            p.limits_max, std::begin(out.world_boundary.limits_max));
        }
      },
      params);
  }

  //! The cooked child record of a compound part; `payload_key` is its cooked
  //! payload, nil for an analytic part.
  [[nodiscard]] auto MakeChildDesc(const ShapePart& part,
    const data::AssetKey& payload_key) -> phys::CompoundShapeChildDesc
  {
    auto child = phys::CompoundShapeChildDesc {};
    child.shape_type = static_cast<uint32_t>(part.type);
    std::ranges::copy(part.local_position, std::begin(child.local_position));
    std::ranges::copy(part.local_rotation, std::begin(child.local_rotation));
    std::ranges::copy(part.local_scale, std::begin(child.local_scale));
    child.payload_asset_key = payload_key;
    std::visit(
      [&](const auto& p) -> void {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, internal::SphereParams>) {
          child.radius = p.radius;
        } else if constexpr (std::is_same_v<T, internal::AxialParams>) {
          child.radius = p.radius;
          child.half_height = p.half_height;
        } else if constexpr (std::is_same_v<T, internal::BoxParams>) {
          std::ranges::copy(p.half_extents, std::begin(child.half_extents));
        } else if constexpr (std::is_same_v<T, internal::PlaneParams>) {
          std::ranges::copy(p.normal, std::begin(child.normal));
          child.distance = p.distance;
        } else if constexpr (std::is_same_v<T, internal::WorldBoundaryParams>) {
          child.boundary_mode = p.mode;
          std::ranges::copy(p.limits_min, std::begin(child.limits_min));
          std::ranges::copy(p.limits_max, std::begin(child.limits_max));
        }
      },
      part.params);
    return child;
  }

  //! The cooked descriptor of `source` without its payload and material.
  [[nodiscard]] auto MakeDescriptor(
    const internal::CollisionShapeSource& source)
    -> phys::CollisionShapeAssetDesc
  {
    auto descriptor = phys::CollisionShapeAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kCollisionShape);
    descriptor.header.version = phys::kCollisionShapeAssetVersion;
    const auto& shape = source.shape;
    descriptor.shape_type = shape.type;
    std::ranges::copy(
      shape.local_position, std::begin(descriptor.local_position));
    std::ranges::copy(
      shape.local_rotation, std::begin(descriptor.local_rotation));
    std::ranges::copy(shape.local_scale, std::begin(descriptor.local_scale));
    descriptor.is_sensor
      = source.is_sensor ? phys::kShapeIsSensorTrue : phys::kShapeIsSensorFalse;
    descriptor.collision_own_layer = source.collision_own_layer;
    descriptor.collision_target_layers = source.collision_target_layers;
    WriteShapeParams(shape.params, shape.type, descriptor.shape_params);
    return descriptor;
  }

} // namespace

auto CollisionShapeDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting collision shape descriptor job: job_id={} path={}",
    JobId(), Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    const auto finalize_start = std::chrono::steady_clock::now();
    auto report = co_await FinalizeSession(session);
    const auto finalize_end = std::chrono::steady_clock::now();
    telemetry.finalize_duration = MakeDuration(finalize_start, finalize_end);
    telemetry.total_duration = MakeDuration(job_start, finalize_end);
    telemetry.io_duration = session.IoDuration();
    telemetry.source_load_duration = session.SourceLoadDuration();
    telemetry.decode_duration = session.DecodeDuration();
    telemetry.load_duration
      = session.SourceLoadDuration() + session.LoadDuration();
    telemetry.cook_duration = session.CookDuration();
    telemetry.emit_duration = session.EmitDuration();
    report.telemetry = telemetry;
    co_return report;
  };
  const auto Fail = [&](ImportSession& session,
                      const std::string_view what) -> co::Co<ImportReport> {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, std::string(what));
    co_return co_await FinalizeWithTelemetry(session);
  };

  EnsureCookedRoot();
  auto& session = Session();

  const auto& request_payload = Request().collision_shape_descriptor;
  if (!request_payload.has_value()) {
    AddDiagnostic(session, Request(), import_codes::kPhysicsShapeRequestInvalid,
      "CollisionShapeDescriptorImportJob requires request "
      "collision_shape_descriptor payload");
    co_return co_await Fail(
      session, "Invalid collision shape descriptor request");
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto prepared = internal::PrepareCollisionShapeDescriptor(
    request_payload->descriptor_json, Request().source_path, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
  if (!prepared.has_value()) {
    co_return co_await Fail(
      session, "Invalid collision shape descriptor payload");
  }
  const auto& source = *prepared;

  auto mounts = internal::LoadMountedCookedSources(
    session, Request(), import_codes::kPhysicsShapeIndexLoadFailed);
  if (session.HasErrors()) {
    co_return co_await Fail(
      session, "Collision shape descriptor context loading failed");
  }

  const auto target = ResolveDescriptorTarget(Request(), session);
  if (!target.has_value()) {
    co_return co_await Fail(
      session, "Collision shape descriptor target resolution failed");
  }

  const auto cook_start = std::chrono::steady_clock::now();
  const auto material_key = LookupAsset(session, Request(), mounts,
    source.material_ref, "/material_ref", data::AssetType::kPhysicsMaterial,
    ReferenceCodes {
      .unresolved = import_codes::kPhysicsShapeMaterialRefUnresolved,
      .wrong_type = import_codes::kPhysicsShapeMaterialRefNotPhysicsMaterial,
    });

  // Every payload cooks before any is emitted, so a failing part emits none.
  auto shape_payload
    = CookPartPayload(session, Request(), mounts, source.shape);
  auto child_payloads = std::vector<std::optional<PartPayload>> {};
  child_payloads.reserve(source.children.size());
  auto children_cooked = true;
  for (const auto& child : source.children) {
    auto payload = CookPartPayload(session, Request(), mounts, child);
    children_cooked &= payload.has_value();
    child_payloads.push_back(
      payload.has_value() ? std::move(*payload) : std::nullopt);
  }
  session.AddCookDuration(
    MakeDuration(cook_start, std::chrono::steady_clock::now()));
  // The mounts keep files of the cooked root open; the job writes into it.
  mounts.clear();
  if (!material_key.has_value() || !shape_payload.has_value()
    || !children_cooked) {
    co_return co_await Fail(session, "Collision shape cooking failed");
  }

  auto descriptor = MakeDescriptor(source);
  descriptor.material_asset_key = *material_key;
  AssetReferenceBuilder references;
  references.AddAsset(*material_key, data::AssetType::kPhysicsMaterial);
  if (shape_payload->has_value()) {
    const auto key = EmitPartPayload(session, Request(),
      std::move(**shape_payload), source.shape.json_pointer);
    if (!key.has_value()) {
      co_return co_await Fail(
        session, "Collision shape payload emission failed");
    }
    descriptor.cooked_shape_ref.payload_asset_key = *key;
    descriptor.cooked_shape_ref.payload_type = (*shape_payload)->type;
    references.AddPhysicsResource(*key);
  }

  auto compound_children = std::vector<phys::CompoundShapeChildDesc> {};
  compound_children.reserve(source.children.size());
  for (size_t i = 0; i < source.children.size(); ++i) {
    const auto& child = source.children.at(i);
    auto payload_key = data::AssetKey {};
    if (auto& payload = child_payloads.at(i); payload.has_value()) {
      const auto key = EmitPartPayload(
        session, Request(), std::move(*payload), child.json_pointer);
      if (!key.has_value()) {
        co_return co_await Fail(
          session, "Collision shape payload emission failed");
      }
      payload_key = *key;
      references.AddPhysicsResource(*key);
    }
    compound_children.push_back(MakeChildDesc(child, payload_key));
  }

  auto& pipeline = CreatePipeline<CollisionShapeImportPipeline>(
    CollisionShapeImportPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
    });
  auto item = CollisionShapeImportPipeline::WorkItem {
    .source_id = target->virtual_path,
    .name = std::filesystem::path(target->relpath).stem().string(),
    .descriptor = descriptor,
    .compound_children = std::move(compound_children),
    .on_started = {},
    .on_finished = {},
    .stop_token = StopToken(),
  };
  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }
  for (auto& diagnostic : result.diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
  if (!result.success) {
    co_return co_await Fail(
      session, "Collision shape descriptor processing failed");
  }

  const auto emit_start = std::chrono::steady_clock::now();
  session.AssetEmitter().Emit(
    data::AssetKey::FromVirtualPath(target->virtual_path),
    data::AssetType::kCollisionShape, target->virtual_path, target->relpath,
    result.descriptor_bytes, std::move(references).Build());
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto CollisionShapeDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
