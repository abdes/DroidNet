//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/HashedBytes.h>
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
#include <Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  namespace phys = data::pak::physics;

  struct DescriptorTarget final {
    std::string relpath;
    std::string virtual_path;
  };

  using AssetTypeMap = std::unordered_map<data::AssetKey, data::AssetType>;

  struct PayloadResolution final {
    data::AssetKey payload_asset_key;
    phys::ShapePayloadType payload_type = phys::ShapePayloadType::kInvalid;
  };

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string json_pointer = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = std::move(json_pointer),
    });
  }

  [[nodiscard]] auto ParseShapeType(const std::string_view value)
    -> phys::ShapeType
  {
    if (value == "sphere") {
      return phys::ShapeType::kSphere;
    }
    if (value == "capsule") {
      return phys::ShapeType::kCapsule;
    }
    if (value == "box") {
      return phys::ShapeType::kBox;
    }
    if (value == "cylinder") {
      return phys::ShapeType::kCylinder;
    }
    if (value == "cone") {
      return phys::ShapeType::kCone;
    }
    if (value == "convex_hull") {
      return phys::ShapeType::kConvexHull;
    }
    if (value == "triangle_mesh") {
      return phys::ShapeType::kTriangleMesh;
    }
    if (value == "height_field") {
      return phys::ShapeType::kHeightField;
    }
    if (value == "plane") {
      return phys::ShapeType::kPlane;
    }
    if (value == "world_boundary") {
      return phys::ShapeType::kWorldBoundary;
    }
    if (value == "compound") {
      return phys::ShapeType::kCompound;
    }
    DCHECK_F(false, "Unsupported shape_type after schema validation");
    return phys::ShapeType::kInvalid;
  }

  [[nodiscard]] auto ParseWorldBoundaryMode(const std::string_view value)
    -> phys::WorldBoundaryMode
  {
    if (value == "aabb_clamp") {
      return phys::WorldBoundaryMode::kAabbClamp;
    }
    if (value == "plane_set") {
      return phys::WorldBoundaryMode::kPlaneSet;
    }
    DCHECK_F(false, "Unsupported boundary_mode after schema validation");
    return phys::WorldBoundaryMode::kInvalid;
  }

  [[nodiscard]] auto PayloadTypeForShapeType(const phys::ShapeType shape_type)
    -> std::optional<phys::ShapePayloadType>
  {
    switch (shape_type) {
    case phys::ShapeType::kConvexHull:
      return phys::ShapePayloadType::kConvex;
    case phys::ShapeType::kTriangleMesh:
      return phys::ShapePayloadType::kMesh;
    case phys::ShapeType::kHeightField:
      return phys::ShapePayloadType::kHeightField;
    case phys::ShapeType::kInvalid:
    case phys::ShapeType::kSphere:
    case phys::ShapeType::kCapsule:
    case phys::ShapeType::kBox:
    case phys::ShapeType::kCylinder:
    case phys::ShapeType::kCone:
    case phys::ShapeType::kPlane:
    case phys::ShapeType::kWorldBoundary:
    case phys::ShapeType::kCompound:
      return std::nullopt;
    }
    return std::nullopt;
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

  [[nodiscard]] auto ResolveAssetKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

  auto BuildProjectedAssetTypeMap(const AssetTypeMap& assets,
    const std::optional<std::pair<data::AssetKey, data::AssetType>>& pending)
    -> AssetTypeMap
  {
    auto out = assets;
    if (pending.has_value()) {
      out.insert_or_assign(pending->first, pending->second);
    }
    return out;
  }

  //! The types of the assets the job's own cooked root lists; empty when
  //! nothing was cooked into it yet. Null when the job reads no such root.
  [[nodiscard]] auto PrimaryAssets(
    const std::span<const internal::MountedCookedSource> mounts,
    const std::filesystem::path& primary_root) -> std::optional<AssetTypeMap>
  {
    const auto primary = std::ranges::find_if(
      mounts, [&](const internal::MountedCookedSource& mount) -> bool {
        return mount.root.lexically_normal() == primary_root.lexically_normal();
      });
    if (primary == mounts.end()) {
      return std::nullopt;
    }
    auto assets = AssetTypeMap {};
    if (primary->content.has_value()) {
      for (const auto& asset : primary->content->Assets()) {
        assets.insert_or_assign(asset.key, asset.type);
      }
    }
    return assets;
  }

  //! Looks up `material_ref`, which names a physics material of the job's own
  //! cooked root.
  auto ResolvePhysicsMaterialKey(ImportSession& session,
    const ImportRequest& request,
    const std::span<const internal::MountedCookedSource> mounts,
    const AssetTypeMap& primary_assets, const std::string_view material_ref,
    data::AssetKey& out_material_key) -> bool
  {
    const auto reference = internal::ResolveJobReference(request, material_ref);
    if (!reference.has_value()) {
      session.AddDiagnostic(internal::RefuseReference(
        request, material_ref, reference.error(), "/material_ref"));
      return false;
    }
    const auto named
      = "'" + reference->written + "' resolves to " + reference->resolved;

    const auto found
      = internal::LookupReference(mounts, request, *reference, std::nullopt);
    if (!found.has_value()) {
      if (found.error().reason == internal::AssetReferenceRefusal::kUnmounted) {
        session.AddDiagnostic(internal::RefuseUnmountedReference(
          request, *reference, "/material_ref"));
      } else {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.material_ref_unresolved",
          "Reference was not found: "
            + internal::DescribeLookup(*reference, found.error()),
          "/material_ref");
      }
      return false;
    }

    const auto primary_it = primary_assets.find(found->key);
    if (primary_it == primary_assets.end()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.reference_source_mismatch",
        "Resolved material_ref is not in the target source domain: " + named,
        "/material_ref");
      return false;
    }

    if (primary_it->second != data::AssetType::kPhysicsMaterial) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.material_ref_not_physics_material",
        "Resolved material_ref has unexpected asset type: " + named,
        "/material_ref");
      return false;
    }

    out_material_key = found->key;
    return true;
  }

#pragma pack(push, 1)
  struct AuthoredShapeBlobHeader final {
    std::array<char, 4> magic = { 'O', 'P', 'S', 'B' };
    uint8_t version = 1U;
    uint8_t shape_type = 0U;
    uint8_t payload_type = 0U;
    uint8_t reserved = 0U;
    uint32_t payload_size = 0U;
  };
#pragma pack(pop)
  static_assert(sizeof(AuthoredShapeBlobHeader) == 12);

  auto SerializeAuthoredShapeBlob(const json& descriptor_doc,
    const phys::ShapeType shape_type, const phys::ShapePayloadType payload_type,
    std::vector<std::byte>& out_bytes) -> bool
  {
    const auto payload = descriptor_doc.dump();
    if (payload.size() > std::numeric_limits<uint32_t>::max()) {
      return false;
    }

    auto header = AuthoredShapeBlobHeader {};
    header.shape_type = static_cast<uint8_t>(shape_type);
    header.payload_type = static_cast<uint8_t>(payload_type);
    header.payload_size = static_cast<uint32_t>(payload.size());

    out_bytes.clear();
    out_bytes.resize(sizeof(header) + payload.size());
    std::memcpy(out_bytes.data(), &header, sizeof(header));
    if (!payload.empty()) {
      std::ranges::copy(std::as_bytes(std::span(payload)),
        std::next(out_bytes.begin(), sizeof(header)));
    }
    return true;
  }

  auto EmitAuthoredShapePayload(ImportSession& session,
    const ImportRequest& request, const json& descriptor_doc,
    const phys::ShapeType shape_type, const phys::ShapePayloadType payload_type)
    -> std::optional<PayloadResolution>
  {
    auto blob_bytes = std::vector<std::byte> {};
    if (!SerializeAuthoredShapeBlob(
          descriptor_doc, shape_type, payload_type, blob_bytes)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.payload_emit_failed",
        "Authored non-analytic shape payload exceeds supported blob size");
      return std::nullopt;
    }

    auto cooked = CookedPhysicsResourcePayload {};
    cooked.data = HashedBytes(
      std::vector<std::byte>(blob_bytes.begin(), blob_bytes.end()));
    cooked.format = phys::PhysicsResourceFormat::kJoltShapeBinary;
    cooked.alignment = 16;

    try {
      const auto emitted
        = session.PhysicsResourceEmitter().Emit(std::move(cooked));
      return PayloadResolution {
        .payload_asset_key = emitted.resource_asset_key,
        .payload_type = payload_type,
      };
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.payload_emit_failed",
        "Failed emitting non-analytic shape payload: "
          + std::string(ex.what()));
      return std::nullopt;
    }
  }

  auto CopyVec3(const json& doc, const std::string_view field,
    const std::span<float, 3> out) -> void
  {
    if (!doc.contains(field)) {
      return;
    }
    const auto values = doc.at(field).get<std::array<float, 3>>();
    std::ranges::copy(values, out.begin());
  }

  auto CopyQuat(const json& doc, const std::string_view field,
    const std::span<float, 4> out) -> void
  {
    if (!doc.contains(field)) {
      return;
    }
    const auto values = doc.at(field).get<std::array<float, 4>>();
    std::ranges::copy(values, out.begin());
  }

  //! Reads the parameters of the shape; the preparation proved they exist and
  //! that world boundary limits are ordered.
  auto ApplyShapeSpecificParameters(const json& descriptor_doc,
    const phys::ShapeType shape_type, phys::CollisionShapeAssetDesc& descriptor)
    -> void
  {
    switch (shape_type) {
    case phys::ShapeType::kSphere:
      descriptor.shape_params.sphere.radius
        = descriptor_doc.at("radius").get<float>();
      break;
    case phys::ShapeType::kCapsule:
      descriptor.shape_params.capsule.radius
        = descriptor_doc.at("radius").get<float>();
      descriptor.shape_params.capsule.half_height
        = descriptor_doc.at("half_height").get<float>();
      break;
    case phys::ShapeType::kBox: {
      const auto half_extents
        = descriptor_doc.at("half_extents").get<std::array<float, 3>>();
      std::ranges::copy(half_extents,
        std::ranges::begin(descriptor.shape_params.box.half_extents));
      break;
    }
    case phys::ShapeType::kCylinder:
      descriptor.shape_params.cylinder.radius
        = descriptor_doc.at("radius").get<float>();
      descriptor.shape_params.cylinder.half_height
        = descriptor_doc.at("half_height").get<float>();
      break;
    case phys::ShapeType::kCone:
      descriptor.shape_params.cone.radius
        = descriptor_doc.at("radius").get<float>();
      descriptor.shape_params.cone.half_height
        = descriptor_doc.at("half_height").get<float>();
      break;
    case phys::ShapeType::kPlane: {
      const auto normal
        = descriptor_doc.at("normal").get<std::array<float, 3>>();
      std::ranges::copy(
        normal, std::ranges::begin(descriptor.shape_params.plane.normal));
      descriptor.shape_params.plane.distance
        = descriptor_doc.at("distance").get<float>();
      break;
    }
    case phys::ShapeType::kWorldBoundary: {
      descriptor.shape_params.world_boundary.boundary_mode
        = ParseWorldBoundaryMode(
          descriptor_doc.at("boundary_mode").get<std::string>());
      const auto limits_min
        = descriptor_doc.at("limits_min").get<std::array<float, 3>>();
      const auto limits_max
        = descriptor_doc.at("limits_max").get<std::array<float, 3>>();
      std::ranges::copy(limits_min,
        std::ranges::begin(descriptor.shape_params.world_boundary.limits_min));
      std::ranges::copy(limits_max,
        std::ranges::begin(descriptor.shape_params.world_boundary.limits_max));
      break;
    }
    case phys::ShapeType::kConvexHull:
    case phys::ShapeType::kTriangleMesh:
    case phys::ShapeType::kHeightField:
    case phys::ShapeType::kCompound:
    case phys::ShapeType::kInvalid:
      break;
    }
  }

  //! Reads one compound child; the preparation proved it is an analytic shape
  //! with every field it needs.
  [[nodiscard]] auto ParseCompoundChild(const json& child_doc)
    -> phys::CompoundShapeChildDesc
  {
    auto out_child = phys::CompoundShapeChildDesc {};
    const auto child_type
      = ParseShapeType(child_doc.at("shape_type").get<std::string>());
    out_child.shape_type = static_cast<uint32_t>(child_type);
    const auto position
      = child_doc.at("local_position").get<std::array<float, 3>>();
    std::ranges::copy(position, std::ranges::begin(out_child.local_position));
    const auto rotation
      = child_doc.at("local_rotation").get<std::array<float, 4>>();
    std::ranges::copy(rotation, std::ranges::begin(out_child.local_rotation));
    const auto scale = child_doc.at("local_scale").get<std::array<float, 3>>();
    std::ranges::copy(scale, std::ranges::begin(out_child.local_scale));

    switch (child_type) {
    case phys::ShapeType::kSphere:
      out_child.radius = child_doc.at("radius").get<float>();
      break;
    case phys::ShapeType::kCapsule:
    case phys::ShapeType::kCylinder:
    case phys::ShapeType::kCone:
      out_child.radius = child_doc.at("radius").get<float>();
      out_child.half_height = child_doc.at("half_height").get<float>();
      break;
    case phys::ShapeType::kBox: {
      const auto half_extents
        = child_doc.at("half_extents").get<std::array<float, 3>>();
      std::ranges::copy(
        half_extents, std::ranges::begin(out_child.half_extents));
      break;
    }
    case phys::ShapeType::kPlane: {
      const auto normal = child_doc.at("normal").get<std::array<float, 3>>();
      std::ranges::copy(normal, std::ranges::begin(out_child.normal));
      out_child.distance = child_doc.at("distance").get<float>();
      break;
    }
    case phys::ShapeType::kWorldBoundary: {
      out_child.boundary_mode = ParseWorldBoundaryMode(
        child_doc.at("boundary_mode").get<std::string>());
      const auto limits_min
        = child_doc.at("limits_min").get<std::array<float, 3>>();
      const auto limits_max
        = child_doc.at("limits_max").get<std::array<float, 3>>();
      std::ranges::copy(limits_min, std::ranges::begin(out_child.limits_min));
      std::ranges::copy(limits_max, std::ranges::begin(out_child.limits_max));
      break;
    }
    case phys::ShapeType::kInvalid:
    case phys::ShapeType::kConvexHull:
    case phys::ShapeType::kTriangleMesh:
    case phys::ShapeType::kHeightField:
    case phys::ShapeType::kCompound:
      DCHECK_F(false, "Compound child is not analytic after preparation");
      break;
    }
    return out_child;
  }

  [[nodiscard]] auto ParseCompoundChildren(const json& descriptor_doc)
    -> std::vector<phys::CompoundShapeChildDesc>
  {
    auto children = std::vector<phys::CompoundShapeChildDesc> {};
    if (!descriptor_doc.contains("children")) {
      return children;
    }
    const auto& child_docs = descriptor_doc.at("children");
    children.reserve(child_docs.size());
    std::ranges::transform(child_docs, std::back_inserter(children),
      [](const json& child_doc) -> phys::CompoundShapeChildDesc {
        return ParseCompoundChild(child_doc);
      });
    return children;
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

  EnsureCookedRoot();
  auto& session = Session();

  const auto& request_payload = Request().collision_shape_descriptor;
  if (!request_payload.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.request_invalid",
      "CollisionShapeDescriptorImportJob requires request "
      "collision_shape_descriptor payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid collision shape descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto prepared = internal::PrepareCollisionShapeDescriptor(
    request_payload->descriptor_json, Request().source_path, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
  if (!prepared.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid collision shape descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }
  const auto& descriptor_doc = *prepared;

  auto mounts
    = internal::LoadMountedCookedSources(session, Request(), "physics.shape.");
  if (session.HasErrors()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor context loading failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto target = ResolveDescriptorTarget(Request(), session);
  if (!target.has_value()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor target resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto primary_source_assets
    = PrimaryAssets(mounts, session.CookedRoot());
  if (!primary_source_assets.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.primary_source_missing",
      "Primary source asset index is not available");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor context loading failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto shape_key = ResolveAssetKey(Request(), target->virtual_path);
  const auto primary_assets = BuildProjectedAssetTypeMap(*primary_source_assets,
    std::pair<data::AssetKey, data::AssetType> {
      shape_key,
      data::AssetType::kCollisionShape,
    });

  // The preparation proved every field read below.
  auto descriptor = phys::CollisionShapeAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kCollisionShape);
  descriptor.header.version = phys::kCollisionShapeAssetVersion;

  const auto shape_type
    = ParseShapeType(descriptor_doc.at("shape_type").get<std::string>());
  descriptor.shape_type = shape_type;

  CopyVec3(descriptor_doc, "local_position", descriptor.local_position);
  CopyQuat(descriptor_doc, "local_rotation", descriptor.local_rotation);
  CopyVec3(descriptor_doc, "local_scale", descriptor.local_scale);

  if (descriptor_doc.contains("is_sensor")) {
    descriptor.is_sensor = descriptor_doc.at("is_sensor").get<bool>()
      ? phys::kShapeIsSensorTrue
      : phys::kShapeIsSensorFalse;
  }
  if (descriptor_doc.contains("collision_own_layer")) {
    descriptor.collision_own_layer
      = descriptor_doc.at("collision_own_layer").get<uint64_t>();
  }
  if (descriptor_doc.contains("collision_target_layers")) {
    descriptor.collision_target_layers
      = descriptor_doc.at("collision_target_layers").get<uint64_t>();
  }
  ApplyShapeSpecificParameters(descriptor_doc, shape_type, descriptor);

  auto compound_children = std::vector<phys::CompoundShapeChildDesc> {};
  if (shape_type == phys::ShapeType::kCompound) {
    compound_children = ParseCompoundChildren(descriptor_doc);
  }

  auto material_key = data::AssetKey {};
  const auto material_ref
    = descriptor_doc.at("material_ref").get<std::string>();
  if (!ResolvePhysicsMaterialKey(session, Request(), mounts, primary_assets,
        material_ref, material_key)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape material_ref resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  descriptor.material_asset_key = material_key;
  // The mounts keep files of the cooked root open; the job writes into it.
  mounts.clear();

  if (const auto payload_type = PayloadTypeForShapeType(shape_type);
    payload_type.has_value()) {
    const auto payload = EmitAuthoredShapePayload(
      session, Request(), descriptor_doc, shape_type, *payload_type);
    if (!payload.has_value()) {
      ReportPhaseProgress(
        ImportPhase::kFailed, 1.0F, "Collision shape payload emission failed");
      co_return co_await FinalizeWithTelemetry(session);
    }

    descriptor.cooked_shape_ref.payload_asset_key = payload->payload_asset_key;
    descriptor.cooked_shape_ref.payload_type = payload->payload_type;
  }

  auto& pipeline = CreatePipeline<CollisionShapeImportPipeline>(
    CollisionShapeImportPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
    });

  AssetReferenceBuilder references;
  references.AddAsset(
    descriptor.material_asset_key, data::AssetType::kPhysicsMaterial);
  references.AddPhysicsResource(descriptor.cooked_shape_ref.payload_asset_key);
  for (const auto& child : compound_children) {
    references.AddPhysicsResource(child.payload_asset_key);
  }
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
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor processing failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  session.AssetEmitter().Emit(shape_key, data::AssetType::kCollisionShape,
    target->virtual_path, target->relpath, result.descriptor_bytes,
    std::move(references).Build());
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
