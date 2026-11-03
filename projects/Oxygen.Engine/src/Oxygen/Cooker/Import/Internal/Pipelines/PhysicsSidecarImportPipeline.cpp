//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Jolt/Jolt.h> // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <ios>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Jolt/Core/Core.h>
#include <Jolt/Core/LinearCurve.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleAntiRollBar.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleDifferential.h>
#include <Jolt/Physics/Vehicle/VehicleEngine.h>
#include <Jolt/Physics/Vehicle/VehicleTrack.h>
#include <Jolt/Physics/Vehicle/VehicleTransmission.h>
#include <Jolt/Physics/Vehicle/Wheel.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/HashedBytes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {
namespace {

  namespace phys = data::pak::physics;
  using core::meta::physics::PhysicsBackend;
  using SidecarResolvedSceneState = detail::ResolvedSceneState;

  //! The prefix of the sidecar's codes, such as `index_load_failed`.
  constexpr auto kPhysicsSidecarCodePrefix = "physics.sidecar.";

  const auto kPhysicsSidecarResolverDiagnostics
    = detail::SidecarSceneResolverDiagnostics {
        .inflight_target_scene_ambiguous_code
        = "physics.sidecar.inflight_target_scene_ambiguous",
        .target_scene_invalid_code = "physics.sidecar.target_scene_invalid",
        .target_scene_not_scene_code = "physics.sidecar.target_scene_not_scene",
        .target_scene_read_failed_code
        = "physics.sidecar.target_scene_read_failed",
        .target_scene_virtual_path_invalid_code
        = "physics.sidecar.target_scene_virtual_path_invalid",
        .target_scene_missing_code = "physics.sidecar.target_scene_missing",
      };

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message)
    -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
    });
  }

  auto AddDiagnosticAtPath(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    const nlohmann::json::json_pointer& location) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = location.to_string(),
    });
  }

  auto ReplaceSceneExtensionWithPhysics(std::string value) -> std::string
  {
    constexpr auto kSceneSuffix = data::kSceneDescriptorExtension;
    constexpr auto kPhysicsSuffix = data::kPhysicsSceneDescriptorExtension;

    if (value.size() >= kSceneSuffix.size() && value.ends_with(kSceneSuffix)) {
      value.resize(value.size() - kSceneSuffix.size());
      value.append(kPhysicsSuffix);
      return value;
    }

    const auto last_slash = value.find_last_of('/');
    const auto dot = value.find_last_of('.');
    if (dot != std::string::npos
      && (last_slash == std::string::npos || dot > last_slash)) {
      value.resize(dot);
    }
    value.append(kPhysicsSuffix);
    return value;
  }

  [[nodiscard]] auto SoftBodyFormatForBackend(const PhysicsBackend backend)
    -> std::optional<phys::PhysicsResourceFormat>
  {
    switch (backend) {
    case PhysicsBackend::kJolt:
      return phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary;
    case PhysicsBackend::kPhysX:
      return phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary;
    case PhysicsBackend::kNone:
      return std::nullopt;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto JointFormatForBackend(const PhysicsBackend backend)
    -> std::optional<phys::PhysicsResourceFormat>
  {
    switch (backend) {
    case PhysicsBackend::kJolt:
      return phys::PhysicsResourceFormat::kJoltConstraintBinary;
    case PhysicsBackend::kPhysX:
      return phys::PhysicsResourceFormat::kPhysXConstraintBinary;
    case PhysicsBackend::kNone:
      return std::nullopt;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto VehicleFormatForBackend(const PhysicsBackend backend)
    -> std::optional<phys::PhysicsResourceFormat>
  {
    switch (backend) {
    case PhysicsBackend::kJolt:
      return phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary;
    case PhysicsBackend::kPhysX:
      return phys::PhysicsResourceFormat::kPhysXVehicleSettingsBinary;
    case PhysicsBackend::kNone:
      return std::nullopt;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto BackendName(const PhysicsBackend backend) -> const char*
  {
    switch (backend) {
    case PhysicsBackend::kJolt:
      return "jolt";
    case PhysicsBackend::kPhysX:
      return "physx";
    case PhysicsBackend::kNone:
      return "none";
    }
    return "unknown";
  }

  [[nodiscard]] auto BackendForSoftBodyFormat(
    const phys::PhysicsResourceFormat format) -> std::optional<PhysicsBackend>
  {
    switch (format) {
    case phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary:
      return PhysicsBackend::kJolt;
    case phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary:
      return PhysicsBackend::kPhysX;
    default:
      return std::nullopt;
    }
  }

  [[nodiscard]] auto BackendForJointFormat(
    const phys::PhysicsResourceFormat format) -> std::optional<PhysicsBackend>
  {
    switch (format) {
    case phys::PhysicsResourceFormat::kJoltConstraintBinary:
      return PhysicsBackend::kJolt;
    case phys::PhysicsResourceFormat::kPhysXConstraintBinary:
      return PhysicsBackend::kPhysX;
    default:
      return std::nullopt;
    }
  }

  using AssetRecordMap
    = std::unordered_map<data::AssetKey, const cooked::CookedAsset*>;

  auto BuildAssetRecordMap(const cooked::CookedContent& content)
    -> AssetRecordMap
  {
    auto out = AssetRecordMap {};
    const auto assets = content.Assets();
    out.reserve(assets.size());
    for (const auto& asset : assets) {
      out.insert_or_assign(asset.key, &asset);
    }
    return out;
  }

  struct BindingValidationContext final {
    observer_ptr<ImportSession> session;
    observer_ptr<const ImportRequest> request;
    std::span<const internal::MountedCookedSource> mounts;
    observer_ptr<const AssetRecordMap> target_assets;
    uint32_t node_count = 0;
  };

  //! The diagnostic codes of a reference that fails to resolve.
  struct ReferenceCodes final {
    //! The virtual path names no cooked asset.
    std::string_view unresolved;
    //! The asset it names has the wrong type.
    std::string_view wrong_type;
  };

  auto ResolveAssetKeyForVirtualPath(BindingValidationContext& ctx,
    std::string_view text, const nlohmann::json::json_pointer& location,
    const data::AssetType expected_type, const ReferenceCodes& codes)
    -> std::optional<data::AssetKey>
  {
    const auto reference = internal::ResolveJobReference(*ctx.request, text);
    if (!reference.has_value()) {
      ctx.session->AddDiagnostic(internal::RefuseReference(
        *ctx.request, text, reference.error(), location.to_string()));
      return std::nullopt;
    }
    const auto named
      = "'" + reference->written + "' resolves to " + reference->resolved;

    const auto found = internal::LookupReference(
      ctx.mounts, *ctx.request, *reference, std::nullopt);
    if (!found.has_value()) {
      if (found.error().reason == internal::AssetReferenceRefusal::kUnmounted) {
        ctx.session->AddDiagnostic(internal::RefuseUnmountedReference(
          *ctx.request, *reference, location.to_string()));
      } else {
        AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
          std::string(codes.unresolved),
          "Reference was not found: "
            + internal::DescribeLookup(*reference, found.error()),
          location);
      }
      return std::nullopt;
    }

    // The bindings complete the target scene: what they name is in the cooked
    // root that holds it.
    const auto target_it = ctx.target_assets->find(found->key);
    if (target_it == ctx.target_assets->end()) {
      AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
        "physics.sidecar.reference_source_mismatch",
        "Resolved reference is not in the target scene source domain: " + named,
        location);
      return std::nullopt;
    }

    if (target_it->second->type != expected_type) {
      AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
        std::string(codes.wrong_type),
        "Resolved reference has unexpected asset type: " + named, location);
      return std::nullopt;
    }

    return found->key;
  }

  auto ValidateNodeIndex(const uint32_t node_index, const uint32_t node_count,
    ImportSession& session, const ImportRequest& request,
    const nlohmann::json::json_pointer& location) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnosticAtPath(session, request, ImportSeverity::kError,
      "physics.sidecar.node_ref_out_of_bounds",
      "Node index is out of bounds for target scene", location);
    return false;
  }

  template <typename BindingSourceT>
  auto ResolveShapeAndMaterialBindings(std::vector<BindingSourceT>& bindings,
    std::string_view category, BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < bindings.size(); ++i) {
      auto& binding = bindings.at(i);
      const auto base_path = nlohmann::json::json_pointer {} / "bindings"
        / std::string(category) / i;
      if (!ValidateNodeIndex(binding.record.node_index, ctx.node_count,
            *ctx.session, *ctx.request, base_path)) {
        continue;
      }

      const auto shape_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.shape_ref,
          base_path / "shape_ref", data::AssetType::kCollisionShape,
          ReferenceCodes {
            .unresolved = "physics.sidecar.shape_ref_unresolved",
            .wrong_type = "physics.sidecar.shape_ref_not_collision_shape",
          });
      const auto material_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.material_ref,
          base_path / "material_ref", data::AssetType::kPhysicsMaterial,
          ReferenceCodes {
            .unresolved = "physics.sidecar.material_ref_unresolved",
            .wrong_type = "physics.sidecar.material_ref_not_physics_material",
          });
      if (shape_key.has_value()) {
        binding.record.shape_asset_key = *shape_key;
      }
      if (material_key.has_value()) {
        binding.record.material_asset_key = *material_key;
      }
    }
  }

  template <typename BindingSourceT>
  auto ResolveShapeOnlyBindings(std::vector<BindingSourceT>& bindings,
    std::string_view category, BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < bindings.size(); ++i) {
      auto& binding = bindings.at(i);
      const auto base_path = nlohmann::json::json_pointer {} / "bindings"
        / std::string(category) / i;
      if (!ValidateNodeIndex(binding.record.node_index, ctx.node_count,
            *ctx.session, *ctx.request, base_path)) {
        continue;
      }

      const auto shape_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.shape_ref,
          base_path / "shape_ref", data::AssetType::kCollisionShape,
          ReferenceCodes {
            .unresolved = "physics.sidecar.shape_ref_unresolved",
            .wrong_type = "physics.sidecar.shape_ref_not_collision_shape",
          });
      if (shape_key.has_value()) {
        binding.record.shape_asset_key = *shape_key;
      }
    }
  }

  auto ResolveCharacterBindings(std::vector<CharacterBindingSource>& bindings,
    BindingValidationContext& ctx) -> void
  {
    ResolveShapeOnlyBindings(bindings, "characters", ctx);
    for (size_t i = 0; i < bindings.size(); ++i) {
      auto& binding = bindings.at(i);
      if (!binding.inner_shape_ref.has_value()) {
        continue;
      }
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "characters" / i;
      const auto inner_shape_key
        = ResolveAssetKeyForVirtualPath(ctx, *binding.inner_shape_ref,
          base_path / "inner_shape_ref", data::AssetType::kCollisionShape,
          ReferenceCodes {
            .unresolved = "physics.sidecar.inner_shape_ref_unresolved",
            .wrong_type = "physics.sidecar.inner_shape_ref_not_collision_shape",
          });
      if (inner_shape_key.has_value()) {
        binding.record.inner_shape_asset_key = *inner_shape_key;
      }
    }
  }

  template <typename RecordT, typename NodeIndexFn>
  auto ValidateNodeBindings(const std::vector<RecordT>& records,
    std::string_view category, const NodeIndexFn& node_index_of,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto path = nlohmann::json::json_pointer {} / "bindings"
        / std::string(category) / i;
      (void)ValidateNodeIndex(node_index_of(records.at(i)), ctx.node_count,
        *ctx.session, *ctx.request, path);
    }
  }

  auto ResolveSoftBodyBindings(std::vector<SoftBodyBindingSource>& records,
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt
      = SoftBodyFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(*ctx.session, *ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for soft-body topology "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i;
      if (!ValidateNodeIndex(records.at(i).record.node_index, ctx.node_count,
            *ctx.session, *ctx.request, base_path / "node_index")) {
        continue;
      }

      const auto source_mesh_key
        = ResolveAssetKeyForVirtualPath(ctx, records.at(i).source_mesh_ref,
          base_path / "source_mesh_ref", data::AssetType::kGeometry,
          ReferenceCodes {
            .unresolved = "physics.sidecar.source_mesh_ref_unresolved",
            .wrong_type = "physics.sidecar.source_mesh_ref_not_geometry",
          });
      if (!source_mesh_key.has_value()) {
        continue;
      }
      records.at(i).source_mesh_asset_key = *source_mesh_key;
      const auto source_mesh_entry = ctx.target_assets->find(*source_mesh_key);
      if (source_mesh_entry == ctx.target_assets->end()) {
        AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
          "physics.sidecar.source_mesh_ref_unresolved",
          "Resolved source mesh has no descriptor in target cooked root",
          base_path / "source_mesh_ref");
        continue;
      }
      records.at(i).source_mesh_references
        = source_mesh_entry->second->references;

      const auto& collision_mesh_ref = records.at(i).collision_mesh_ref;
      if (collision_mesh_ref) {
        (void)ResolveAssetKeyForVirtualPath(ctx, *collision_mesh_ref,
          base_path / "collision_mesh_ref", data::AssetType::kGeometry,
          ReferenceCodes {
            .unresolved = "physics.sidecar.collision_mesh_ref_unresolved",
            .wrong_type = "physics.sidecar.collision_mesh_ref_not_geometry",
          });
        if (requested_backend == PhysicsBackend::kJolt) {
          AddDiagnosticAtPath(*ctx.session, *ctx.request,
            ImportSeverity::kError, "physics.sidecar.payload_invalid",
            "soft_bodies.collision_mesh_ref is not supported for jolt backend",
            base_path / "collision_mesh_ref");
          continue;
        }
      }

      if (records.at(i).backend_explicit) {
        const auto authored_backend
          = BackendForSoftBodyFormat(records.at(i).record.topology_format);
        if (!authored_backend.has_value()
          || *authored_backend != requested_backend) {
          AddDiagnosticAtPath(*ctx.session, *ctx.request,
            ImportSeverity::kError, "physics.sidecar.backend_mismatch",
            "soft-body backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            base_path / "backend" / "target");
          continue;
        }
      }
      records.at(i).record.topology_format = *expected_format_opt;
    }
  }

  auto ResolveJointBindings(std::vector<JointBindingSource>& records,
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt = JointFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(*ctx.session, *ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for joint constraint "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "joints" / i;
      const bool node_a_ok
        = ValidateNodeIndex(records.at(i).record.node_index_a, ctx.node_count,
          *ctx.session, *ctx.request, base_path / "node_index_a");
      const bool node_b_ok
        = (records.at(i).record.node_index_b == phys::kWorldAttachmentNodeIndex)
        || ValidateNodeIndex(records.at(i).record.node_index_b, ctx.node_count,
          *ctx.session, *ctx.request, base_path / "node_index_b");
      if (!(node_a_ok && node_b_ok)) {
        continue;
      }

      if (records.at(i).backend_explicit) {
        const auto authored_backend
          = BackendForJointFormat(records.at(i).constraint_format);
        if (!authored_backend.has_value()
          || *authored_backend != requested_backend) {
          AddDiagnosticAtPath(*ctx.session, *ctx.request,
            ImportSeverity::kError, "physics.sidecar.backend_mismatch",
            "joint backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            base_path / "backend" / "target");
          continue;
        }
      }
      records.at(i).constraint_format = *expected_format_opt;
    }
  }

  auto ResolveVehicleBindings(std::vector<VehicleBindingSource>& records,
    std::vector<phys::VehicleWheelBindingRecord>& wheel_records,
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt = VehicleFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(*ctx.session, *ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for vehicle constraint "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

    wheel_records.clear();
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i;
      if (!ValidateNodeIndex(records.at(i).record.node_index, ctx.node_count,
            *ctx.session, *ctx.request, base_path / "node_index")) {
        continue;
      }

      // The preparation guarantees the wheels: at least two, each distinct.
      DCHECK_F(records.at(i).wheels.size() >= 2U);

      const auto wheel_offset = wheel_records.size();
      auto slice_count = uint32_t { 0 };
      for (size_t w = 0; w < records.at(i).wheels.size(); ++w) {
        const auto wheel_path = base_path / "wheels" / w;
        const auto& wheel = records.at(i).wheels.at(w);
        if (!ValidateNodeIndex(wheel.node_index, ctx.node_count, *ctx.session,
              *ctx.request, wheel_path / "node_index")) {
          continue;
        }
        if (wheel.backend_target != PhysicsBackend::kNone
          && wheel.backend_target != requested_backend) {
          AddDiagnosticAtPath(*ctx.session, *ctx.request,
            ImportSeverity::kError, "physics.sidecar.backend_mismatch",
            "vehicle wheel backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            wheel_path / "backend" / "target");
          continue;
        }

        wheel_records.push_back(phys::VehicleWheelBindingRecord {
          .vehicle_node_index = records.at(i).record.node_index,
          .wheel_node_index = wheel.node_index,
          .axle_index = wheel.axle_index,
          .side = wheel.side,
          .backend_scalars = wheel.backend_scalars,
        });
        slice_count += 1U;
      }

      if (slice_count < 2U) {
        AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Vehicle wheel bindings must resolve to at least two valid wheels",
          base_path / "wheels");
        wheel_records.resize(wheel_offset);
        continue;
      }

      if (wheel_offset > std::numeric_limits<uint32_t>::max()) {
        AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Vehicle wheel table offset exceeds uint32 range", base_path);
        wheel_records.resize(wheel_offset);
        continue;
      }

      records.at(i).record.wheel_slice_offset
        = static_cast<uint32_t>(wheel_offset);
      records.at(i).record.wheel_slice_count = slice_count;
      records.at(i).constraint_format = *expected_format_opt;
    }
  }

  auto ResolveRequestedPhysicsBackend(const ImportRequest& request)
    -> PhysicsBackend
  {
    return request.options.physics.backend;
  }

  auto EnsureJoltAllocatorReady() -> void
  {
    static std::once_flag once {};
    std::call_once(once, [] -> void { JPH::RegisterDefaultAllocator(); });
  }

  [[nodiscard]] auto JsonFloatOr(
    const nlohmann::json& obj, const char* key, const float fallback) -> float
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    return obj.at(key).get<float>();
  }

  [[nodiscard]] auto JsonVec3Or(const nlohmann::json& obj, const char* key,
    const JPH::Vec3& fallback) -> JPH::Vec3
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    const auto values = obj.at(key).get<std::array<float, 3>>();
    return { values.at(0), values.at(1), values.at(2) };
  }

  auto ApplyCurve2DToLinearCurve(
    const nlohmann::json& points, JPH::LinearCurve& out_curve) -> bool
  {
    if (!points.is_array()) {
      return false;
    }
    out_curve.Clear();
    out_curve.Reserve(static_cast<JPH::uint>(points.size()));
    for (const auto& point : points) {
      if (!point.is_array() || point.size() != 2U) {
        return false;
      }
      out_curve.AddPoint(point.at(0).get<float>(), point.at(1).get<float>());
    }
    out_curve.Sort();
    return true;
  }

  auto AssignBlobBytes(
    const std::string_view blob, std::vector<std::byte>& out_bytes) -> void
  {
    const auto bytes = std::as_bytes(std::span { blob });
    out_bytes.assign(bytes.begin(), bytes.end());
  }

  auto SerializeJoltConstraintSettings(const JPH::ConstraintSettings& settings,
    std::vector<std::byte>& out_bytes) -> bool
  {
    auto stream = std::ostringstream(std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    AssignBlobBytes(blob, out_bytes);
    return true;
  }

  auto SerializeJoltVehicleSettings(
    const JPH::VehicleConstraintSettings& settings,
    std::vector<std::byte>& out_bytes) -> bool
  {
    auto stream = std::ostringstream(std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    AssignBlobBytes(blob, out_bytes);
    return true;
  }

  auto SerializeJoltSoftBodySettings(
    const JPH::SoftBodySharedSettings& settings, std::vector<std::byte>& out)
    -> bool
  {
    auto stream = std::ostringstream(std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    AssignBlobBytes(blob, out);
    return true;
  }

  [[nodiscard]] auto ToJoltConstraintSpace(
    const nlohmann::json& authored_binding) -> JPH::EConstraintSpace
  {
    if (authored_binding.contains("constraint_space")
      && authored_binding.at("constraint_space").get<std::string>()
        == "local") {
      return JPH::EConstraintSpace::LocalToBodyCOM;
    }
    return JPH::EConstraintSpace::WorldSpace;
  }

  auto ParseJoltJointType(
    const nlohmann::json& authored_binding, std::string& out_type) -> void
  {
    out_type = authored_binding.contains("constraint_type")
      ? authored_binding.at("constraint_type").get<std::string>()
      : std::string("fixed");
  }

  auto CookJoltJointBlob(const JointBindingSource& source,
    std::vector<std::byte>& out_blob, std::string& error) -> bool
  {
    EnsureJoltAllocatorReady();
    const auto& authored = source.authored_binding;
    const auto space = ToJoltConstraintSpace(authored);
    const auto point_a
      = JsonVec3Or(authored, "local_frame_a_position", JPH::Vec3::sZero());
    const auto point_b
      = JsonVec3Or(authored, "local_frame_b_position", JPH::Vec3::sZero());

    auto type = std::string {};
    ParseJoltJointType(authored, type);
    if (type == "fixed") {
      auto settings = JPH::FixedConstraintSettings {};
      settings.mSpace = space;
      settings.mAutoDetectPoint = false;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "point") {
      auto settings = JPH::PointConstraintSettings {};
      settings.mSpace = space;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "distance") {
      auto settings = JPH::DistanceConstraintSettings {};
      settings.mSpace = space;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      if (authored.contains("limits_lower")) {
        const auto limits
          = authored.at("limits_lower").get<std::array<float, 6>>();
        settings.mMinDistance = limits.at(0);
      }
      if (authored.contains("limits_upper")) {
        const auto limits
          = authored.at("limits_upper").get<std::array<float, 6>>();
        settings.mMaxDistance = limits.at(0);
      }
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "hinge") {
      auto settings = JPH::HingeConstraintSettings {};
      settings.mSpace = space;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "slider") {
      auto settings = JPH::SliderConstraintSettings {};
      settings.mSpace = space;
      settings.mAutoDetectPoint = false;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "cone") {
      auto settings = JPH::ConeConstraintSettings {};
      settings.mSpace = space;
      settings.mPoint1 = JPH::RVec3(point_a);
      settings.mPoint2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      if (authored.contains("limits_upper")) {
        const auto limits
          = authored.at("limits_upper").get<std::array<float, 6>>();
        settings.mHalfConeAngle = std::max(0.0F, limits.at(3));
      }
      return SerializeJoltConstraintSettings(settings, out_blob);
    }
    if (type == "six_dof") {
      auto settings = JPH::SixDOFConstraintSettings {};
      settings.mSpace = space;
      settings.mPosition1 = JPH::RVec3(point_a);
      settings.mPosition2 = JPH::RVec3(point_b);
      settings.mNumVelocityStepsOverride
        = source.record.backend_scalars.jolt.num_velocity_steps_override;
      settings.mNumPositionStepsOverride
        = source.record.backend_scalars.jolt.num_position_steps_override;
      if (authored.contains("limits_lower")
        && authored.contains("limits_upper")) {
        const auto lower
          = authored.at("limits_lower").get<std::array<float, 6>>();
        const auto upper
          = authored.at("limits_upper").get<std::array<float, 6>>();
        for (int axis = 0; axis < JPH::SixDOFConstraintSettings::EAxis::Num;
          ++axis) {
          settings.SetLimitedAxis(
            static_cast<JPH::SixDOFConstraintSettings::EAxis>(axis),
            lower.at(axis), upper.at(axis));
        }
      }
      return SerializeJoltConstraintSettings(settings, out_blob);
    }

    error = "unsupported joint constraint_type '" + type + "'";
    return false;
  }

  auto ToJoltLraType(const phys::SoftBodyTetherMode tether_mode)
    -> JPH::SoftBodySharedSettings::ELRAType
  {
    switch (tether_mode) {
    case phys::SoftBodyTetherMode::kEuclidean:
      return JPH::SoftBodySharedSettings::ELRAType::EuclideanDistance;
    case phys::SoftBodyTetherMode::kGeodesic:
      return JPH::SoftBodySharedSettings::ELRAType::GeodesicDistance;
    case phys::SoftBodyTetherMode::kNone:
      return JPH::SoftBodySharedSettings::ELRAType::None;
    }
    return JPH::SoftBodySharedSettings::ELRAType::None;
  }

  struct SoftBodySurfaceMesh final {
    std::vector<JPH::Float3> vertices;
    std::vector<JPH::SoftBodySharedSettings::Face> faces;
    //! Each source vertex's welded vertex, or `kNotOnSurface` when no face
    //! uses it; filled by `WeldSurfaceMesh`.
    std::vector<uint32_t> welded_of_source;
  };

  constexpr auto kNotOnSurface = std::numeric_limits<uint32_t>::max();

  //! Why a soft body's topology failed to cook: a diagnostic code and the
  //! reason it reports.
  struct SoftBodyCookError final {
    std::string_view code = "physics.sidecar.resource_serialize_failed";
    std::string reason;
  };

  struct SoftBodyGeometryTopologyInput final {
    enum class Kind : uint8_t {
      kStandard,
      kProcedural,
    };

    Kind kind = Kind::kStandard;
    data::pak::core::ResourceIndexT vertex_buffer
      = data::pak::core::kNoResourceIndex;
    data::pak::core::ResourceIndexT index_buffer
      = data::pak::core::kNoResourceIndex;
    std::string procedural_name;
    std::vector<std::byte> procedural_params;
    std::vector<data::pak::geometry::MeshViewDesc> mesh_views;
  };

  auto ParseGeometryTopologyInput(std::span<const std::byte> descriptor_bytes,
    const data::AssetReferences& references, SoftBodyGeometryTopologyInput& out,
    std::string& error) -> bool
  {
    using data::MeshType;
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    const auto asset_desc_read
      = serio::CopyAt<GeometryAssetDesc>(descriptor_bytes);
    if (!asset_desc_read) {
      error = "geometry descriptor is too small for GeometryAssetDesc";
      return false;
    }
    const auto& asset_desc = *asset_desc_read;
    if (asset_desc.lod_count == 0U) {
      error = "geometry descriptor has lod_count=0";
      return false;
    }

    auto offset = sizeof(GeometryAssetDesc);
    const auto mesh_desc_read
      = serio::CopyAt<MeshDesc>(descriptor_bytes, serio::ByteOffset { offset });
    if (!mesh_desc_read) {
      error = "geometry descriptor is too small for MeshDesc";
      return false;
    }
    const auto& mesh_desc = *mesh_desc_read;
    const auto reserves_zero = data::pak::geometry::HasZeroReserves(mesh_desc);
    if (!reserves_zero) {
      error = "geometry descriptor mesh reserved bytes are not zero";
      return false;
    }
    offset += sizeof(MeshDesc);

    const auto mesh_type = static_cast<MeshType>(mesh_desc.mesh_type);
    if (mesh_type == MeshType::kStandard) {
      out.kind = SoftBodyGeometryTopologyInput::Kind::kStandard;
      const auto vertex = references.ResolveResource(
        mesh_desc.info.standard.vertex_buffer, data::ResourceKind::kBuffer);
      const auto index = references.ResolveResource(
        mesh_desc.info.standard.index_buffer, data::ResourceKind::kBuffer);
      if (!vertex || !index) {
        error = "Soft-body geometry has invalid resource references";
        return false;
      }
      out.vertex_buffer = vertex->value_or(data::pak::core::kNoResourceIndex);
      out.index_buffer = index->value_or(data::pak::core::kNoResourceIndex);
    } else if (mesh_type == MeshType::kProcedural) {
      out.kind = SoftBodyGeometryTopologyInput::Kind::kProcedural;
      try {
        out.procedural_name = std::string {
          data::DescriptorStrings::FromDescriptor(
            descriptor_bytes, asset_desc.header.strings)
            .Get(mesh_desc.name_offset),
        };
      } catch (const std::invalid_argument&) {
        error = "geometry descriptor procedural mesh name is invalid";
        return false;
      }
      const auto params_size
        = static_cast<size_t>(mesh_desc.info.procedural.params_size);
      if (offset > descriptor_bytes.size()
        || descriptor_bytes.size() - offset < params_size) {
        error = "geometry descriptor procedural params exceed descriptor size";
        return false;
      }
      const auto params = descriptor_bytes.subspan(offset, params_size);
      out.procedural_params.assign(params.begin(), params.end());
      offset += params_size;
    } else {
      error = "soft-body source mesh must be standard or procedural geometry";
      return false;
    }

    out.mesh_views.clear();
    for (uint32_t submesh_index = 0; submesh_index < mesh_desc.submesh_count;
      ++submesh_index) {
      const auto submesh_desc_read = serio::CopyAt<SubMeshDesc>(
        descriptor_bytes, serio::ByteOffset { offset });
      if (!submesh_desc_read) {
        error = "geometry descriptor submesh table exceeds descriptor size";
        return false;
      }
      const auto& submesh_desc = *submesh_desc_read;
      offset += sizeof(SubMeshDesc);

      for (uint32_t view_index = 0; view_index < submesh_desc.mesh_view_count;
        ++view_index) {
        const auto view_desc = serio::CopyAt<MeshViewDesc>(
          descriptor_bytes, serio::ByteOffset { offset });
        if (!view_desc) {
          error = "geometry descriptor mesh view table exceeds descriptor size";
          return false;
        }
        offset += sizeof(MeshViewDesc);
        out.mesh_views.push_back(*view_desc);
      }
    }

    if (out.mesh_views.empty()) {
      error = "soft-body source mesh has no mesh views";
      return false;
    }
    return true;
  }

  //! Reads row `index` of `buffers` and its payload.
  auto ReadBufferPayload(const cooked::CookedResourceTable& buffers,
    const data::pak::core::ResourceIndexT index,
    data::pak::core::BufferResourceDesc& descriptor,
    std::vector<std::byte>& payload, std::string& error) -> bool
  {
    if (index == data::pak::core::kNoResourceIndex) {
      error = "geometry references kNoResourceIndex buffer";
      return false;
    }
    if (index.get() >= buffers.RowCount()) {
      error = "geometry references out-of-range buffer resource index";
      return false;
    }

    try {
      descriptor = buffers.Row<data::pak::core::BufferResourceDesc>(index);
      payload = buffers.Payload(index);
    } catch (const std::out_of_range&) {
      error = "buffer payload range exceeds the buffers data";
      return false;
    } catch (const std::exception& ex) {
      error = std::string("buffer resource cannot be read: ") + ex.what();
      return false;
    }
    return true;
  }

  auto DecodeVertexPositions(
    const data::pak::core::BufferResourceDesc& descriptor,
    const std::span<const std::byte> payload,
    std::vector<JPH::Float3>& vertices, std::string& error) -> bool
  {
    if (descriptor.element_format != static_cast<uint8_t>(Format::kUnknown)) {
      error = "vertex buffer must be structured (element_format=unknown)";
      return false;
    }
    if (descriptor.element_stride < sizeof(float) * 3U) {
      error = "vertex buffer element_stride is too small for position xyz";
      return false;
    }
    const auto stride = static_cast<size_t>(descriptor.element_stride);
    if (payload.size() % stride != 0U) {
      error = "vertex buffer payload size is not aligned to element_stride";
      return false;
    }

    const auto count = payload.size() / stride;
    vertices.clear();
    vertices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      auto record = serio::ByteReader { payload.subspan(i * stride, stride) };
      const auto x = record.Read<float>();
      const auto y = record.Read<float>();
      const auto z = record.Read<float>();
      if (!x || !y || !z) {
        error = "vertex buffer element_stride is too small for position xyz";
        return false;
      }
      if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
        error = "vertex buffer contains non-finite positions";
        return false;
      }
      vertices.emplace_back(*x, *y, *z);
    }

    if (vertices.empty()) {
      error = "vertex buffer payload is empty";
      return false;
    }
    return true;
  }

  auto DecodeIndexBuffer(const data::pak::core::BufferResourceDesc& descriptor,
    const std::span<const std::byte> payload, std::vector<uint32_t>& indices,
    std::string& error) -> bool
  {
    size_t element_size = 0U;
    const auto element_format = static_cast<Format>(descriptor.element_format);
    if (element_format == Format::kR32UInt) {
      element_size = sizeof(uint32_t);
    } else if (element_format == Format::kR16UInt) {
      element_size = sizeof(uint16_t);
    } else if (element_format == Format::kUnknown
      && (descriptor.element_stride == sizeof(uint32_t)
        || descriptor.element_stride == sizeof(uint16_t))) {
      element_size = static_cast<size_t>(descriptor.element_stride);
    } else {
      error = "index buffer must be uint16 or uint32";
      return false;
    }

    if (payload.size() % element_size != 0U) {
      error = "index buffer payload size is not aligned to element size";
      return false;
    }

    const auto count = payload.size() / element_size;
    indices.clear();
    indices.reserve(count);
    auto reader = serio::ByteReader { payload };
    for (size_t i = 0; i < count; ++i) {
      if (element_size == sizeof(uint32_t)) {
        const auto value = reader.Read<uint32_t>();
        if (!value) {
          error = "index buffer payload size is not aligned to element size";
          return false;
        }
        indices.push_back(*value);
      } else {
        const auto value = reader.Read<uint16_t>();
        if (!value) {
          error = "index buffer payload size is not aligned to element size";
          return false;
        }
        indices.push_back(static_cast<uint32_t>(*value));
      }
    }

    if (indices.empty()) {
      error = "index buffer payload is empty";
      return false;
    }
    return true;
  }

  [[nodiscard]] auto IsFiniteFloat3Cook(const JPH::Float3& value) noexcept
    -> bool
  {
    return std::isfinite(value.x) && std::isfinite(value.y)
      && std::isfinite(value.z);
  }

  [[nodiscard]] auto EdgeLengthSquared(
    const JPH::Float3& a, const JPH::Float3& b) noexcept -> float
  {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    const auto dz = a.z - b.z;
    return (dx * dx) + (dy * dy) + (dz * dz);
  }

  [[nodiscard]] auto TriangleDoubleAreaSquared(const JPH::Float3& a,
    const JPH::Float3& b, const JPH::Float3& c) noexcept -> float
  {
    const auto abx = b.x - a.x;
    const auto aby = b.y - a.y;
    const auto abz = b.z - a.z;
    const auto acx = c.x - a.x;
    const auto acy = c.y - a.y;
    const auto acz = c.z - a.z;
    const auto cx = (aby * acz) - (abz * acy);
    const auto cy = (abz * acx) - (abx * acz);
    const auto cz = (abx * acy) - (aby * acx);
    return (cx * cx) + (cy * cy) + (cz * cz);
  }

  auto BuildFacesFromMeshViews(const std::vector<uint32_t>& indices,
    std::span<const data::pak::geometry::MeshViewDesc> mesh_views,
    std::span<const JPH::Float3> vertices,
    std::vector<JPH::SoftBodySharedSettings::Face>& faces, std::string& error)
    -> bool
  {
    if (vertices.empty()) {
      error = "mesh has no vertices";
      return false;
    }
    auto bounds_min = vertices.front();
    auto bounds_max = vertices.front();
    for (const auto& v : vertices) {
      if (!IsFiniteFloat3Cook(v)) {
        error = "mesh contains non-finite vertex positions";
        return false;
      }
      bounds_min.x = std::min(bounds_min.x, v.x);
      bounds_min.y = std::min(bounds_min.y, v.y);
      bounds_min.z = std::min(bounds_min.z, v.z);
      bounds_max.x = std::max(bounds_max.x, v.x);
      bounds_max.y = std::max(bounds_max.y, v.y);
      bounds_max.z = std::max(bounds_max.z, v.z);
    }

    const auto max_extent = std::max({
      bounds_max.x - bounds_min.x,
      bounds_max.y - bounds_min.y,
      bounds_max.z - bounds_min.z,
    });
    const auto min_edge_length = std::max(max_extent * 1.0e-6F, 1.0e-8F);
    const auto min_edge_length_sq = min_edge_length * min_edge_length;
    const auto min_area2_sq = min_edge_length_sq * min_edge_length_sq;
    const auto vertex_count = vertices.size();

    faces.clear();
    auto rejected_degenerate_faces = size_t { 0 };
    for (const auto& view : mesh_views) {
      const auto first_index = static_cast<size_t>(view.first_index);
      const auto index_count = static_cast<size_t>(view.index_count);
      const auto first_vertex = static_cast<uint32_t>(view.first_vertex);
      const auto view_vertex_count = static_cast<uint32_t>(view.vertex_count);

      if (index_count == 0U) {
        continue;
      }
      if (index_count % 3U != 0U) {
        error = "mesh view index_count is not divisible by 3";
        return false;
      }
      if (first_index > indices.size()
        || indices.size() - first_index < index_count) {
        error = "mesh view index span exceeds decoded index buffer";
        return false;
      }
      if (first_vertex > vertex_count
        || static_cast<size_t>(first_vertex) + view_vertex_count
          > vertex_count) {
        error = "mesh view vertex span exceeds decoded vertex buffer";
        return false;
      }

      for (size_t i = 0; i < index_count; i += 3U) {
        const auto local0 = indices.at(first_index + i + 0U);
        const auto local1 = indices.at(first_index + i + 1U);
        const auto local2 = indices.at(first_index + i + 2U);

        if (local0 >= view_vertex_count || local1 >= view_vertex_count
          || local2 >= view_vertex_count) {
          error = "mesh view contains index outside its vertex range";
          return false;
        }

        const auto v0 = first_vertex + local0;
        const auto v1 = first_vertex + local1;
        const auto v2 = first_vertex + local2;
        if (v0 >= vertex_count || v1 >= vertex_count || v2 >= vertex_count) {
          error = "mesh view index resolves outside decoded vertex buffer";
          return false;
        }

        const auto& p0 = oxygen::base::CheckedAt(vertices, v0);
        const auto& p1 = oxygen::base::CheckedAt(vertices, v1);
        const auto& p2 = oxygen::base::CheckedAt(vertices, v2);
        const auto e01 = EdgeLengthSquared(p0, p1);
        const auto e12 = EdgeLengthSquared(p1, p2);
        const auto e20 = EdgeLengthSquared(p2, p0);
        if (e01 <= min_edge_length_sq || e12 <= min_edge_length_sq
          || e20 <= min_edge_length_sq
          || TriangleDoubleAreaSquared(p0, p1, p2) <= min_area2_sq) {
          ++rejected_degenerate_faces;
          continue;
        }

        faces.emplace_back(v0, v1, v2, 0U);
      }
    }

    if (faces.empty()) {
      if (rejected_degenerate_faces > 0U) {
        error = "mesh yielded only degenerate triangles after sanitation";
      } else {
        error = "mesh did not yield any triangle faces";
      }
      return false;
    }
    if (rejected_degenerate_faces > 0U) {
      DLOG_F(1,
        "Soft-body source mesh sanitation dropped {} degenerate triangles",
        rejected_degenerate_faces);
    }
    return true;
  }

  auto BuildSurfaceMeshFromProcedural(
    const SoftBodyGeometryTopologyInput& topology, SoftBodySurfaceMesh& out,
    std::string& error) -> bool
  {
    const auto generated = data::GenerateMeshBuffers(topology.procedural_name,
      std::span<const std::byte>(
        topology.procedural_params.data(), topology.procedural_params.size()));
    if (!generated.has_value()) {
      error = "failed generating procedural geometry source mesh";
      return false;
    }

    const auto& source_vertices = generated->first;
    const auto& source_indices = generated->second;
    out.vertices.clear();
    out.vertices.reserve(source_vertices.size());
    for (const auto& v : source_vertices) {
      out.vertices.emplace_back(v.position.x, v.position.y, v.position.z);
    }

    return BuildFacesFromMeshViews(
      source_indices, topology.mesh_views, out.vertices, out.faces, error);
  }

  auto BuildSurfaceMeshFromStandard(
    const SoftBodyGeometryTopologyInput& topology,
    const cooked::CookedContent& content, SoftBodySurfaceMesh& out,
    std::string& error) -> bool
  {
    const auto& buffers = content.ResourceTable(data::ResourceKind::kBuffer);
    auto vertex_descriptor = data::pak::core::BufferResourceDesc {};
    auto index_descriptor = data::pak::core::BufferResourceDesc {};
    auto vertex_payload = std::vector<std::byte> {};
    auto index_payload = std::vector<std::byte> {};

    if (!ReadBufferPayload(buffers, topology.vertex_buffer, vertex_descriptor,
          vertex_payload, error)) {
      return false;
    }
    if (!ReadBufferPayload(buffers, topology.index_buffer, index_descriptor,
          index_payload, error)) {
      return false;
    }

    auto decoded_indices = std::vector<uint32_t> {};
    if (!DecodeVertexPositions(
          vertex_descriptor, vertex_payload, out.vertices, error)) {
      return false;
    }
    if (!DecodeIndexBuffer(
          index_descriptor, index_payload, decoded_indices, error)) {
      return false;
    }
    if (!BuildFacesFromMeshViews(decoded_indices, topology.mesh_views,
          out.vertices, out.faces, error)) {
      return false;
    }
    return true;
  }

  auto BuildSoftBodySurfaceMesh(const SoftBodyBindingSource& source,
    const cooked::CookedContent& target_content, SoftBodySurfaceMesh& out,
    std::string& error) -> bool
  {
    auto descriptor_bytes = std::vector<std::byte> {};
    try {
      descriptor_bytes
        = target_content.Descriptor(source.source_mesh_asset_key);
    } catch (const std::exception& ex) {
      error = std::string("soft-body source mesh descriptor cannot be read: ")
        + ex.what();
      return false;
    }

    auto topology = SoftBodyGeometryTopologyInput {};
    if (!ParseGeometryTopologyInput(
          descriptor_bytes, source.source_mesh_references, topology, error)) {
      return false;
    }

    switch (topology.kind) {
    case SoftBodyGeometryTopologyInput::Kind::kStandard:
      return BuildSurfaceMeshFromStandard(topology, target_content, out, error);
    case SoftBodyGeometryTopologyInput::Kind::kProcedural:
      return BuildSurfaceMeshFromProcedural(topology, out, error);
    }

    error = "soft-body source mesh kind is unsupported";
    return false;
  }

  //! Welds the surface by position: vertices at the same position (with -0
  //! and +0 equal) become one, and vertices no face uses are dropped.
  /*!
   Render meshes split vertices along UV seams, at poles and on hard edges;
   a soft body needs one particle per surface point, or it tears along every
   split. Faces keep their order and winding.
  */
  auto WeldSurfaceMesh(SoftBodySurfaceMesh& mesh) -> void
  {
    using PositionKey = std::array<uint32_t, 3>;
    const auto key_of = [](const JPH::Float3& p) -> PositionKey {
      return {
        std::bit_cast<uint32_t>(p.x + 0.0F),
        std::bit_cast<uint32_t>(p.y + 0.0F),
        std::bit_cast<uint32_t>(p.z + 0.0F),
      };
    };
    auto welded_ids = std::map<PositionKey, uint32_t> {};
    auto welded = std::vector<JPH::Float3> {};
    mesh.welded_of_source.assign(mesh.vertices.size(), kNotOnSurface);
    for (auto& face : mesh.faces) {
      for (auto& index : face.mVertex) {
        auto& target = mesh.welded_of_source.at(index);
        if (target == kNotOnSurface) {
          const auto& position = mesh.vertices.at(index);
          const auto [it, inserted] = welded_ids.try_emplace(
            key_of(position), static_cast<uint32_t>(welded.size()));
          if (inserted) {
            welded.push_back(position);
          }
          target = it->second;
        }
        index = target;
      }
    }
    mesh.vertices = std::move(welded);
  }

  //! Checks a welded surface is one oriented 2-manifold piece: every edge
  //! borders at most two faces, running opposite ways, and every face is
  //! reachable from every other. With `closed`, every edge borders two.
  auto ValidateSurfaceTopology(const SoftBodySurfaceMesh& mesh,
    const bool closed, SoftBodyCookError& error) -> bool
  {
    auto directed = std::map<std::pair<uint32_t, uint32_t>, uint32_t> {};
    auto parent = std::vector<uint32_t>(mesh.vertices.size());
    std::ranges::iota(parent, 0U);
    const auto root = [&parent](uint32_t id) -> uint32_t {
      while (parent.at(id) != id) {
        parent.at(id) = parent.at(parent.at(id));
        id = parent.at(id);
      }
      return id;
    };
    for (const auto& face : mesh.faces) {
      if (face.mVertex[0] == face.mVertex[1]
        || face.mVertex[1] == face.mVertex[2]
        || face.mVertex[2] == face.mVertex[0]) {
        error = {
          .code = "physics.sidecar.soft_body_surface_not_manifold",
          .reason = "a face collapses once its vertices are welded",
        };
        return false;
      }
      for (size_t k = 0; k < 3U; ++k) {
        const auto from = face.mVertex[k];
        const auto to = face.mVertex[(k + 1U) % 3U];
        if (++directed[{ from, to }] > 1U) {
          error = {
            .code = "physics.sidecar.soft_body_surface_not_manifold",
            .reason = "an edge borders more than two faces, or two faces "
                      "that wind the same way",
          };
          return false;
        }
        parent.at(root(from)) = root(to);
      }
    }
    if (closed) {
      for (const auto& [edge, count] : directed) {
        if (!directed.contains({ edge.second, edge.first })) {
          error = {
            .code = "physics.sidecar.soft_body_surface_not_closed",
            .reason = "pressure needs a closed surface, and an edge borders "
                      "one face",
          };
          return false;
        }
      }
    }
    auto components = size_t { 0 };
    for (uint32_t id = 0; id < parent.size(); ++id) {
      components += root(id) == id ? 1U : 0U;
    }
    if (components != 1U) {
      error = {
        .code = "physics.sidecar.soft_body_surface_disconnected",
        .reason
        = "the surface has " + std::to_string(components) + " separate parts",
      };
      return false;
    }
    return true;
  }

  //! Makes the authored source vertices kinematic on the welded surface.
  /*!
   Pins name source mesh vertices, so they stay valid whatever the welding
   does; each must be on the surface. The sidecar schema keeps each list free
   of duplicates.
  */
  auto ApplyKinematicVertices(const SoftBodySurfaceMesh& mesh,
    const std::vector<uint32_t>& source_vertices, const std::string_view field,
    JPH::SoftBodySharedSettings& settings, SoftBodyCookError& error) -> bool
  {
    for (const auto source : source_vertices) {
      if (source >= mesh.welded_of_source.size()
        || mesh.welded_of_source.at(source) == kNotOnSurface) {
        error = {
          .code = "physics.sidecar.soft_body_pin_invalid",
          .reason = std::string(field) + " names vertex "
            + std::to_string(source)
            + ", which no face of the source mesh uses",
        };
        return false;
      }
      settings.mVertices.at(mesh.welded_of_source.at(source)).mInvMass = 0.0F;
    }
    return true;
  }

  constexpr float kMinCookedVolumeCompliance = 1.0e-6F;
  constexpr double kMinAbsCookedSurfaceRestVolume = 1.0e-8;

  [[nodiscard]] auto ComputeSignedSurfaceVolume(
    const JPH::SoftBodySharedSettings& settings) noexcept -> double
  {
    double signed_six_volume = 0.0;
    for (const auto& face : settings.mFaces) {
      const auto& p0 = settings.mVertices.at(face.mVertex[0]).mPosition;
      const auto& p1 = settings.mVertices.at(face.mVertex[1]).mPosition;
      const auto& p2 = settings.mVertices.at(face.mVertex[2]).mPosition;
      signed_six_volume
        += (static_cast<double>(p0.x)
             * ((static_cast<double>(p1.y) * static_cast<double>(p2.z))
               - (static_cast<double>(p1.z) * static_cast<double>(p2.y))))
        + (static_cast<double>(p0.y)
          * ((static_cast<double>(p1.z) * static_cast<double>(p2.x))
            - (static_cast<double>(p1.x) * static_cast<double>(p2.z))))
        + (static_cast<double>(p0.z)
          * ((static_cast<double>(p1.x) * static_cast<double>(p2.y))
            - (static_cast<double>(p1.y) * static_cast<double>(p2.x))));
    }
    return signed_six_volume / 6.0;
  }

  auto ValidateCookedSoftBodySettings(
    const JPH::SoftBodySharedSettings& settings,
    const float pressure_coefficient, std::string& error) -> bool
  {
    for (const auto& volume : settings.mVolumeConstraints) {
      if (!std::isfinite(volume.mCompliance)
        || volume.mCompliance < kMinCookedVolumeCompliance) {
        error = "volume_compliance must be finite and >= "
          + std::to_string(kMinCookedVolumeCompliance)
          + " for volumetric soft-body constraints";
        return false;
      }
    }

    if (pressure_coefficient > 0.0F) {
      if (settings.mFaces.empty()) {
        error = "pressure_coefficient > 0 requires a non-empty closed surface";
        return false;
      }
      // A closed surface wound outward (`clip::FrontFaceCCW`, normals out)
      // encloses a positive volume; a negative one is wound inward.
      const auto signed_volume = ComputeSignedSurfaceVolume(settings);
      if (!std::isfinite(signed_volume)
        || signed_volume < kMinAbsCookedSurfaceRestVolume) {
        error = "pressure_coefficient > 0 requires a finite positive rest "
                "volume: a closed surface wound outward";
        return false;
      }
    }
    return true;
  }

  auto CookJoltSoftBodyBlob(const SoftBodyBindingSource& source,
    const cooked::CookedContent& target_content,
    std::vector<std::byte>& out_blob, SoftBodyCookError& error) -> bool
  {
    EnsureJoltAllocatorReady();

    auto surface_mesh = SoftBodySurfaceMesh {};
    if (!BuildSoftBodySurfaceMesh(
          source, target_content, surface_mesh, error.reason)) {
      return false;
    }
    WeldSurfaceMesh(surface_mesh);
    if (!ValidateSurfaceTopology(
          surface_mesh, source.record.pressure_coefficient > 0.0F, error)) {
      return false;
    }

    auto shared_settings = JPH::Ref<JPH::SoftBodySharedSettings> {
      new JPH::SoftBodySharedSettings()
    };
    if (shared_settings == nullptr) {
      error.reason = "failed to allocate Jolt soft-body shared settings";
      return false;
    }
    shared_settings->mVertices.reserve(surface_mesh.vertices.size());
    for (const auto& v : surface_mesh.vertices) {
      shared_settings->mVertices.emplace_back(
        v, JPH::Float3(0.0F, 0.0F, 0.0F), 1.0F);
    }
    shared_settings->mFaces.clear();
    shared_settings->mFaces.reserve(
      static_cast<JPH::uint>(surface_mesh.faces.size()));
    for (const auto& face : surface_mesh.faces) {
      shared_settings->mFaces.push_back(face);
    }

    const auto attrs = JPH::SoftBodySharedSettings::VertexAttributes(
      source.record.edge_compliance, source.record.shear_compliance,
      source.record.bend_compliance, ToJoltLraType(source.record.tether_mode),
      source.record.tether_max_distance_multiplier);
    shared_settings->CreateConstraints(&attrs, 1U);

    for (auto& volume : shared_settings->mVolumeConstraints) {
      volume.mCompliance = source.record.volume_compliance;
    }

    if (!ApplyKinematicVertices(surface_mesh, source.pinned_vertices,
          "pinned_vertices", *shared_settings, error)
      || !ApplyKinematicVertices(surface_mesh, source.kinematic_vertices,
        "kinematic_vertices", *shared_settings, error)) {
      return false;
    }

    shared_settings->CalculateEdgeLengths();
    shared_settings->CalculateBendConstraintConstants();
    shared_settings->CalculateVolumeConstraintVolumes();
    shared_settings->CalculateLRALengths(
      source.record.tether_max_distance_multiplier);
    if (!ValidateCookedSoftBodySettings(
          *shared_settings, source.record.pressure_coefficient, error.reason)) {
      return false;
    }
    shared_settings->Optimize();
    return SerializeJoltSoftBodySettings(*shared_settings, out_blob);
  }

  auto ApplyVehicleEngineSettings(
    const nlohmann::json& authored, JPH::VehicleEngineSettings& out) -> bool
  {
    if (!authored.contains("engine")) {
      return true;
    }
    const auto& engine = authored.at("engine");
    out.mMaxTorque = JsonFloatOr(engine, "max_torque", out.mMaxTorque);
    out.mMinRPM = JsonFloatOr(engine, "rpm_min", out.mMinRPM);
    out.mMaxRPM = JsonFloatOr(engine, "rpm_max", out.mMaxRPM);
    out.mInertia = JsonFloatOr(engine, "inertia", out.mInertia);
    if (engine.contains("torque_curve")) {
      return ApplyCurve2DToLinearCurve(
        engine.at("torque_curve"), out.mNormalizedTorque);
    }
    return true;
  }

  auto ApplyVehicleTransmissionSettings(const nlohmann::json& authored,
    JPH::VehicleTransmissionSettings& out) -> bool
  {
    if (!authored.contains("transmission")) {
      return true;
    }
    const auto& transmission = authored.at("transmission");
    if (transmission.contains("shift_mode")) {
      const auto mode = transmission.at("shift_mode").get<std::string>();
      out.mMode = mode == "manual" ? JPH::ETransmissionMode::Manual
                                   : JPH::ETransmissionMode::Auto;
    }
    if (transmission.contains("forward_gear_ratios")) {
      out.mGearRatios.clear();
      for (const auto& ratio : transmission.at("forward_gear_ratios")) {
        out.mGearRatios.push_back(ratio.get<float>());
      }
    }
    if (transmission.contains("reverse_gear_ratios")) {
      out.mReverseGearRatios.clear();
      for (const auto& ratio : transmission.at("reverse_gear_ratios")) {
        out.mReverseGearRatios.push_back(ratio.get<float>());
      }
    }
    out.mClutchStrength
      = JsonFloatOr(transmission, "clutch_strength", out.mClutchStrength);
    out.mShiftUpRPM
      = JsonFloatOr(transmission, "shift_up_rpm", out.mShiftUpRPM);
    out.mShiftDownRPM
      = JsonFloatOr(transmission, "shift_down_rpm", out.mShiftDownRPM);
    out.mClutchReleaseTime = JsonFloatOr(
      transmission, "clutch_engagement_time", out.mClutchReleaseTime);
    return true;
  }

  auto CookJoltVehicleBlob(const VehicleBindingSource& source,
    std::vector<std::byte>& out_blob, std::string& error) -> bool
  {
    EnsureJoltAllocatorReady();
    const auto wheel_count = source.wheels.size();
    if (wheel_count < 2U) {
      error = "vehicle must resolve at least two wheels before blob cooking";
      return false;
    }

    auto settings = JPH::VehicleConstraintSettings {};
    settings.mUp
      = JPH::Vec3(space::move::Up.x, space::move::Up.y, space::move::Up.z);
    settings.mForward = JPH::Vec3(
      space::move::Forward.x, space::move::Forward.y, space::move::Forward.z);

    const auto& authored = source.authored_binding;
    const auto& authored_wheels = authored.at("wheels");
    if (!authored_wheels.is_array() || authored_wheels.size() != wheel_count) {
      error
        = "vehicle wheels authored payload does not match resolved wheel table";
      return false;
    }

    const auto tracked
      = source.record.controller_type == phys::VehicleControllerType::kTracked;
    for (size_t i = 0; i < wheel_count; ++i) {
      const auto& authored_wheel = authored_wheels.at(i);
      if (tracked) {
        auto wheel
          = JPH::Ref<JPH::WheelSettingsTV> { new JPH::WheelSettingsTV() };
        wheel->mRadius = JsonFloatOr(authored_wheel, "radius", wheel->mRadius);
        wheel->mWidth = JsonFloatOr(authored_wheel, "width", wheel->mWidth);
        settings.mWheels.push_back(
          JPH::Ref<JPH::WheelSettings> { wheel.GetPtr() });
      } else {
        auto wheel
          = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
        wheel->mRadius = JsonFloatOr(authored_wheel, "radius", wheel->mRadius);
        wheel->mWidth = JsonFloatOr(authored_wheel, "width", wheel->mWidth);
        wheel->mInertia
          = JsonFloatOr(authored_wheel, "rotational_inertia", wheel->mInertia);
        wheel->mMaxSteerAngle = JsonFloatOr(
          authored_wheel, "max_steering_angle", wheel->mMaxSteerAngle);
        wheel->mMaxBrakeTorque = JsonFloatOr(
          authored_wheel, "max_brake_torque", wheel->mMaxBrakeTorque);
        wheel->mMaxHandBrakeTorque = JsonFloatOr(
          authored_wheel, "max_hand_brake_torque", wheel->mMaxHandBrakeTorque);
        if (authored_wheel.contains("longitudinal_friction_curve")
          && !ApplyCurve2DToLinearCurve(
            authored_wheel.at("longitudinal_friction_curve"),
            wheel->mLongitudinalFriction)) {
          error = "invalid wheel longitudinal_friction_curve payload";
          return false;
        }
        if (authored_wheel.contains("lateral_friction_curve")
          && !ApplyCurve2DToLinearCurve(
            authored_wheel.at("lateral_friction_curve"),
            wheel->mLateralFriction)) {
          error = "invalid wheel lateral_friction_curve payload";
          return false;
        }
        settings.mWheels.push_back(
          JPH::Ref<JPH::WheelSettings> { wheel.GetPtr() });
      }
    }

    if (tracked) {
      auto controller = JPH::Ref<JPH::TrackedVehicleControllerSettings> {
        new JPH::TrackedVehicleControllerSettings()
      };
      if (!ApplyVehicleEngineSettings(authored, controller->mEngine)
        || !ApplyVehicleTransmissionSettings(
          authored, controller->mTransmission)) {
        error = "invalid tracked vehicle engine/transmission settings";
        return false;
      }

      auto left = std::vector<uint32_t> {};
      auto right = std::vector<uint32_t> {};
      left.reserve(wheel_count);
      right.reserve(wheel_count);
      for (size_t i = 0; i < wheel_count; ++i) {
        if (source.wheels.at(i).side == phys::VehicleWheelSide::kLeft) {
          left.push_back(static_cast<uint32_t>(i));
        } else {
          right.push_back(static_cast<uint32_t>(i));
        }
      }
      if (left.empty() || right.empty()) {
        error = "tracked vehicle must provide left and right wheel groups";
        return false;
      }
      auto& left_track
        = controller->mTracks[static_cast<int>(JPH::ETrackSide::Left)];
      left_track.mDrivenWheel = left.front();
      left_track.mWheels.clear();
      left_track.mWheels.reserve(static_cast<JPH::uint>(left.size()));
      for (const auto wheel_index : left) {
        left_track.mWheels.push_back(static_cast<JPH::uint>(wheel_index));
      }
      auto& right_track
        = controller->mTracks[static_cast<int>(JPH::ETrackSide::Right)];
      right_track.mDrivenWheel = right.front();
      right_track.mWheels.clear();
      right_track.mWheels.reserve(static_cast<JPH::uint>(right.size()));
      for (const auto wheel_index : right) {
        right_track.mWheels.push_back(static_cast<JPH::uint>(wheel_index));
      }
      settings.mController = controller;
    } else {
      auto controller = JPH::Ref<JPH::WheeledVehicleControllerSettings> {
        new JPH::WheeledVehicleControllerSettings()
      };
      if (!ApplyVehicleEngineSettings(authored, controller->mEngine)
        || !ApplyVehicleTransmissionSettings(
          authored, controller->mTransmission)) {
        error = "invalid wheeled vehicle engine/transmission settings";
        return false;
      }

      if (authored.contains("differentials")) {
        controller->mDifferentials.clear();
        for (const auto& differential : authored.at("differentials")) {
          auto out = JPH::VehicleDifferentialSettings {};
          out.mLeftWheel = differential.at("left_wheel_index").get<int>();
          out.mRightWheel = differential.at("right_wheel_index").get<int>();
          out.mDifferentialRatio = JsonFloatOr(
            differential, "differential_ratio", out.mDifferentialRatio);
          out.mLeftRightSplit
            = JsonFloatOr(differential, "torque_split", out.mLeftRightSplit);
          out.mLimitedSlipRatio = JsonFloatOr(
            differential, "limited_slip_ratio", out.mLimitedSlipRatio);
          out.mEngineTorqueRatio = JsonFloatOr(
            differential, "engine_torque_ratio", out.mEngineTorqueRatio);
          controller->mDifferentials.push_back(out);
        }
      } else if (wheel_count >= 2U) {
        auto differential = JPH::VehicleDifferentialSettings {};
        differential.mLeftWheel = 0;
        differential.mRightWheel = 1;
        differential.mEngineTorqueRatio = 1.0F;
        controller->mDifferentials.push_back(differential);
      }

      settings.mController = controller;
    }

    if (authored.contains("anti_roll_bars")) {
      settings.mAntiRollBars.clear();
      for (const auto& anti_roll : authored.at("anti_roll_bars")) {
        auto out = JPH::VehicleAntiRollBar {};
        out.mLeftWheel = anti_roll.at("left_wheel_index").get<int>();
        out.mRightWheel = anti_roll.at("right_wheel_index").get<int>();
        out.mStiffness = JsonFloatOr(anti_roll, "stiffness", out.mStiffness);
        settings.mAntiRollBars.push_back(out);
      }
    }

    return SerializeJoltVehicleSettings(settings, out_blob);
  }

  auto CookSoftBodyTopologyBlob(const SoftBodyBindingSource& source,
    const cooked::CookedContent& target_content,
    std::vector<std::byte>& out_blob, SoftBodyCookError& error) -> bool
  {
    switch (source.record.topology_format) {
    case phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary:
      return CookJoltSoftBodyBlob(source, target_content, out_blob, error);
    case phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary:
      error.reason = "physx soft-body cooking is not implemented";
      return false;
    default:
      error.reason
        = "soft-body topology format does not match any supported backend";
      return false;
    }
  }

  auto CookJointConstraintBlob(const JointBindingSource& source,
    std::vector<std::byte>& out_blob, std::string& error) -> bool
  {
    switch (source.constraint_format) {
    case phys::PhysicsResourceFormat::kJoltConstraintBinary:
      return CookJoltJointBlob(source, out_blob, error);
    case phys::PhysicsResourceFormat::kPhysXConstraintBinary:
      error = "physx joint constraint cooking is not implemented";
      return false;
    default:
      error = "joint constraint format does not match any supported backend";
      return false;
    }
  }

  auto CookVehicleConstraintBlob(const VehicleBindingSource& source,
    std::vector<std::byte>& out_blob, std::string& error) -> bool
  {
    switch (source.constraint_format) {
    case phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary:
      return CookJoltVehicleBlob(source, out_blob, error);
    case phys::PhysicsResourceFormat::kPhysXVehicleSettingsBinary:
      error = "physx vehicle settings cooking is not implemented";
      return false;
    default:
      error = "vehicle constraint format does not match any supported backend";
      return false;
    }
  }

  auto BuildCookedPhysicsPayload(const std::vector<std::byte>& blob_bytes,
    const phys::PhysicsResourceFormat format) -> CookedPhysicsResourcePayload
  {
    auto payload = CookedPhysicsResourcePayload {};
    payload.data = HashedBytes(blob_bytes);
    payload.format = format;
    payload.alignment = 16;
    return payload;
  }

  auto EmitBindingBlob(ImportSession& session, const ImportRequest& request,
    const nlohmann::json::json_pointer& location,
    const std::vector<std::byte>& blob_bytes,
    const phys::PhysicsResourceFormat format, data::AssetKey& out_asset_key)
    -> bool
  {
    try {
      const auto emitted = session.PhysicsResourceEmitter().Emit(
        BuildCookedPhysicsPayload(blob_bytes, format));
      out_asset_key = emitted.resource_asset_key;
      return true;
    } catch (const std::exception& ex) {
      AddDiagnosticAtPath(session, request, ImportSeverity::kError,
        "physics.sidecar.resource_emit_failed",
        "Failed emitting backend-cooked physics payload: "
          + std::string(ex.what()),
        location);
      return false;
    }
  }

  auto EmitSoftBodyTopologyResources(
    std::vector<SoftBodyBindingSource>& records, ImportSession& session,
    const ImportRequest& request, const cooked::CookedContent& target_content)
    -> bool
  {
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = SoftBodyCookError {};
      if (!CookSoftBodyTopologyBlob(
            records.at(i), target_content, blob_bytes, cook_error)) {
        LOG_F(ERROR,
          "PhysicsSidecarImportPipeline: soft-body topology cooking failed "
          "(source_mesh_ref='{}' code='{}' reason='{}')",
          records.at(i).source_mesh_ref, cook_error.code, cook_error.reason);
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(cook_error.code),
          "Failed to cook soft-body topology resource: " + cook_error.reason,
          nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i);
        continue;
      }
      if (!EmitBindingBlob(session, request,
            nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i,
            blob_bytes, records.at(i).record.topology_format,
            records.at(i).record.topology_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitJointConstraintResources(std::vector<JointBindingSource>& records,
    ImportSession& session, const ImportRequest& request) -> bool
  {
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookJointConstraintBlob(records.at(i), blob_bytes, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.resource_serialize_failed",
          "Failed to cook joint constraint resource: " + cook_error,
          nlohmann::json::json_pointer {} / "bindings" / "joints" / i);
        continue;
      }
      if (!EmitBindingBlob(session, request,
            nlohmann::json::json_pointer {} / "bindings" / "joints" / i,
            blob_bytes, records.at(i).constraint_format,
            records.at(i).record.constraint_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitVehicleConstraintResources(
    std::vector<VehicleBindingSource>& records, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookVehicleConstraintBlob(records.at(i), blob_bytes, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.resource_serialize_failed",
          "Failed to cook vehicle constraint resource: " + cook_error,
          nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i);
        continue;
      }
      if (!EmitBindingBlob(session, request,
            nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i,
            blob_bytes, records.at(i).constraint_format,
            records.at(i).record.constraint_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitBackendCookedBindingResources(PhysicsSidecarDocument& parsed,
    ImportSession& session, const ImportRequest& request,
    const cooked::CookedContent& target_content) -> bool
  {
    return EmitSoftBodyTopologyResources(
             parsed.soft_bodies, session, request, target_content)
      && EmitJointConstraintResources(parsed.joints, session, request)
      && EmitVehicleConstraintResources(parsed.vehicles, session, request);
  }

  template <typename SourceT, typename RecordT>
  auto ExtractRecordVector(const std::vector<SourceT>& source,
    RecordT SourceT::* record_member) -> std::vector<RecordT>
  {
    auto records = std::vector<RecordT> {};
    records.reserve(source.size());
    for (const auto& row : source) {
      records.push_back(row.*record_member);
    }
    return records;
  }

  struct TableBlob final {
    phys::PhysicsBindingType binding_type = phys::PhysicsBindingType::kUnknown;
    uint32_t entry_size = 0;
    uint32_t record_count = 0;
    std::vector<std::byte> bytes;
  };

  template <typename RecordT>
  auto MakeTableBlob(const phys::PhysicsBindingType binding_type,
    const std::vector<RecordT>& records) -> std::optional<TableBlob>
  {
    static_assert(std::is_trivially_copyable_v<RecordT>);
    if (records.empty()) {
      return std::nullopt;
    }
    auto blob = TableBlob {
      .binding_type = binding_type,
      .entry_size = static_cast<uint32_t>(sizeof(RecordT)),
      .record_count = static_cast<uint32_t>(records.size()),
      .bytes = {},
    };
    blob.bytes.resize(records.size() * sizeof(RecordT));
    std::memcpy(blob.bytes.data(), records.data(), blob.bytes.size());
    return blob;
  }

  template <typename RecordT, typename LessFn>
  auto SortAndAppendTable(std::vector<TableBlob>& tables,
    const phys::PhysicsBindingType binding_type, std::vector<RecordT>& records,
    LessFn&& less) -> void
  {
    std::ranges::sort(records, std::forward<LessFn>(less));
    if (const auto table = MakeTableBlob(binding_type, records);
      table.has_value()) {
      tables.push_back(*table);
    }
  }

  auto MakeSoftBodyTableBlob(const std::vector<SoftBodyBindingSource>& records)
    -> std::optional<TableBlob>
  {
    if (records.empty()) {
      return std::nullopt;
    }

    auto blob = TableBlob {
      .binding_type = phys::PhysicsBindingType::kSoftBody,
      .entry_size = static_cast<uint32_t>(sizeof(phys::SoftBodyBindingRecord)),
      .record_count = static_cast<uint32_t>(records.size()),
      .bytes = {},
    };

    for (const auto& source : records) {
      auto record = source.record;
      auto trailing_cursor = static_cast<uint64_t>(sizeof(record));

      const auto pinned_size_bytes
        = static_cast<uint64_t>(source.pinned_vertices.size())
        * sizeof(uint32_t);
      const auto kinematic_size_bytes
        = static_cast<uint64_t>(source.kinematic_vertices.size())
        * sizeof(uint32_t);
      if (source.pinned_vertices.empty()) {
        record.pinned_vertex_byte_offset = 0U;
      } else if (trailing_cursor > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
      } else {
        record.pinned_vertex_byte_offset
          = static_cast<uint32_t>(trailing_cursor);
        trailing_cursor += pinned_size_bytes;
      }

      if (source.kinematic_vertices.empty()) {
        record.kinematic_vertex_byte_offset = 0U;
      } else if (trailing_cursor > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
      } else {
        record.kinematic_vertex_byte_offset
          = static_cast<uint32_t>(trailing_cursor);
        trailing_cursor += kinematic_size_bytes;
      }

      const auto fixed_size = sizeof(record);
      const auto previous_size = blob.bytes.size();
      blob.bytes.resize(
        previous_size + fixed_size + pinned_size_bytes + kinematic_size_bytes);

      const auto dest = std::span { blob.bytes };
      std::memcpy(
        dest.subspan(previous_size, fixed_size).data(), &record, fixed_size);
      auto write_cursor = previous_size + fixed_size;
      if (pinned_size_bytes > 0U) {
        std::memcpy(
          dest.subspan(write_cursor, static_cast<size_t>(pinned_size_bytes))
            .data(),
          source.pinned_vertices.data(),
          static_cast<size_t>(pinned_size_bytes));
        write_cursor += static_cast<size_t>(pinned_size_bytes);
      }
      if (kinematic_size_bytes > 0U) {
        std::memcpy(
          dest.subspan(write_cursor, static_cast<size_t>(kinematic_size_bytes))
            .data(),
          source.kinematic_vertices.data(),
          static_cast<size_t>(kinematic_size_bytes));
      }
    }

    return blob;
  }

  //! The backend a physics scene records for the import backend `backend`.
  [[nodiscard]] constexpr auto PakBackendOf(
    const PhysicsBackend backend) noexcept -> phys::PhysicsBackend
  {
    switch (backend) {
    case PhysicsBackend::kJolt:
      return phys::PhysicsBackend::kJolt;
    case PhysicsBackend::kPhysX:
      return phys::PhysicsBackend::kPhysX;
    case PhysicsBackend::kNone:
      break;
    }
    return phys::PhysicsBackend::kNone;
  }

  auto SerializePhysicsSceneAsset(const SidecarResolvedSceneState& scene_state,
    std::string_view sidecar_name, const std::vector<TableBlob>& input_tables,
    const PhysicsBackend backend, std::string& error)
    -> std::optional<std::vector<std::byte>>
  {
    auto tables = input_tables;
    std::ranges::sort(
      tables, [](const TableBlob& lhs, const TableBlob& rhs) -> bool {
        return static_cast<uint32_t>(lhs.binding_type)
          < static_cast<uint32_t>(rhs.binding_type);
      });

    auto desc = phys::PhysicsSceneAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
    desc.header.version = phys::kPhysicsSceneAssetVersion;
    auto strings = data::DescriptorStringsBuilder {};
    desc.header.name_offset = strings.Add(sidecar_name);
    desc.target_scene_key = scene_state.scene_key;
    desc.target_node_count = scene_state.node_count;
    // Every binding was resolved for this backend, so its arm of each
    // backend-scalar union is the one written.
    desc.backend = PakBackendOf(backend);
    const auto scene_hash
      = data::ComputeDescriptorHash(scene_state.source_scene_descriptor);
    std::copy_n(scene_hash.begin(), std::size(desc.target_scene_content_hash),
      std::begin(desc.target_scene_content_hash));
    desc.component_table_count = static_cast<uint32_t>(tables.size());
    desc.component_table_directory_offset = tables.empty()
      ? 0U
      : static_cast<data::pak::core::OffsetT>(sizeof(desc));

    auto directory_entries = std::vector<phys::PhysicsComponentTableDesc> {};
    directory_entries.reserve(tables.size());

    uint64_t cursor = tables.empty() ? 0U
                                     : static_cast<uint64_t>(sizeof(desc))
        + (static_cast<uint64_t>(tables.size())
          * sizeof(phys::PhysicsComponentTableDesc));

    for (const auto& table : tables) {
      if (table.record_count == 0U) {
        error = "Physics table has zero records";
        return std::nullopt;
      }
      const auto min_table_size = static_cast<uint64_t>(table.record_count)
        * static_cast<uint64_t>(table.entry_size);
      if (table.binding_type != phys::PhysicsBindingType::kSoftBody) {
        if (table.bytes.size() != min_table_size) {
          error = "Physics table payload size mismatch";
          return std::nullopt;
        }
      } else if (static_cast<uint64_t>(table.bytes.size()) < min_table_size) {
        error = "Soft-body table payload is truncated";
        return std::nullopt;
      }
      if (cursor > std::numeric_limits<data::pak::core::OffsetT>::max()) {
        error = "Physics table offset exceeds OffsetT range";
        return std::nullopt;
      }
      directory_entries.push_back({
        .binding_type = table.binding_type,
        .table = data::pak::core::ResourceTable {
          .offset = static_cast<data::pak::core::OffsetT>(cursor),
          .count = table.record_count,
          .entry_size = table.entry_size,
        },
      });
      cursor += table.bytes.size();
    }

    auto stream = serio::MemoryStream {};
    auto writer = serio::Writer(stream);
    auto packed = writer.ScopedAlignment(1);
    if (!writer
          .WriteBlob(std::as_bytes(
            std::span<const phys::PhysicsSceneAssetDesc, 1>(&desc, 1)))
          .has_value()) {
      error = "Failed writing PhysicsSceneAssetDesc";
      return std::nullopt;
    }
    if (!directory_entries.empty()) {
      if (!writer.WriteBlob(std::as_bytes(std::span(directory_entries)))
            .has_value()) {
        error = "Failed writing physics component table directory";
        return std::nullopt;
      }
      for (const auto& table : tables) {
        if (!writer.WriteBlob(std::span<const std::byte>(table.bytes))
              .has_value()) {
          error = "Failed writing physics component table payload";
          return std::nullopt;
        }
      }
    }

    const auto stream_data = stream.Data();
    auto bytes = std::vector<std::byte>(stream_data.begin(), stream_data.end());
    data::AppendDescriptorStrings(bytes, strings);
    return bytes;
  }

  auto ValidatePhysicsSidecarRequest(
    ImportSession& session, const ImportRequest& request) -> bool
  {
    if (!request.physics.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.request.invalid_import_kind",
        "Physics sidecar import requires ImportRequest::physics payload");
      return false;
    }
    if (request.physics->target_scene_virtual_path.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.request.target_scene_virtual_path_missing",
        "Physics sidecar import requires target_scene_virtual_path");
      return false;
    }
    if (!internal::IsCanonicalVirtualPath(
          request.physics->target_scene_virtual_path)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.target_scene_virtual_path_invalid",
        "Target scene virtual path must be canonical");
      return false;
    }
    return true;
  }

  auto ResolveAndValidateBindings(PhysicsSidecarDocument& parsed,
    const SidecarResolvedSceneState& resolved_scene_state,
    std::span<const internal::MountedCookedSource> mounts,
    ImportSession& session, const ImportRequest& request) -> bool
  {
    const auto* target_content
      = detail::FindSceneContent(mounts, resolved_scene_state.scene_key);
    if (target_content == nullptr) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.target_scene_missing",
        "Resolved target scene key is not present in cooked scene context");
      return false;
    }

    const auto target_assets = BuildAssetRecordMap(*target_content);
    const auto requested_backend = ResolveRequestedPhysicsBackend(request);
    if (requested_backend == PhysicsBackend::kNone) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend 'none' is not valid for sidecar cooking");
      return false;
    }
    auto validation_ctx = BindingValidationContext {
      .session = observer_ptr { &session },
      .request = observer_ptr { &request },
      .mounts = mounts,
      .target_assets = observer_ptr { &target_assets },
      .node_count = resolved_scene_state.node_count,
    };

    ResolveShapeAndMaterialBindings(
      parsed.rigid_bodies, "rigid_bodies", validation_ctx);
    ResolveShapeAndMaterialBindings(
      parsed.colliders, "colliders", validation_ctx);
    ResolveCharacterBindings(parsed.characters, validation_ctx);
    ResolveSoftBodyBindings(
      parsed.soft_bodies, requested_backend, validation_ctx);
    ResolveJointBindings(parsed.joints, requested_backend, validation_ctx);
    ResolveVehicleBindings(parsed.vehicles, parsed.vehicle_wheels,
      requested_backend, validation_ctx);
    ValidateNodeBindings(
      parsed.aggregates, "aggregates",
      [](const auto& record) -> auto { return record.node_index; },
      validation_ctx);
    if (session.HasErrors()) {
      return false;
    }
    return EmitBackendCookedBindingResources(
      parsed, session, request, *target_content);
  }

  auto BuildPhysicsSidecarTables(const PhysicsSidecarDocument& parsed,
    AssetReferenceBuilder& references) -> std::vector<TableBlob>
  {
    auto rigid_records = ExtractRecordVector(
      parsed.rigid_bodies, &RigidBodyBindingSource::record);
    auto collider_records
      = ExtractRecordVector(parsed.colliders, &ColliderBindingSource::record);
    auto character_records
      = ExtractRecordVector(parsed.characters, &CharacterBindingSource::record);
    auto soft_body_records = parsed.soft_bodies;
    auto joint_records
      = ExtractRecordVector(parsed.joints, &JointBindingSource::record);
    auto vehicle_sources = parsed.vehicles;
    std::ranges::sort(
      vehicle_sources, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.record.node_index < rhs.record.node_index;
      });
    auto vehicle_records = std::vector<phys::VehicleBindingRecord> {};
    auto wheel_records = std::vector<phys::VehicleWheelBindingRecord> {};
    vehicle_records.reserve(vehicle_sources.size());
    for (const auto& source : vehicle_sources) {
      auto record = source.record;
      if (wheel_records.size()
        > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        continue;
      }

      auto sorted_wheels = source.wheels;
      std::ranges::sort(
        sorted_wheels, [](const auto& lhs, const auto& rhs) -> auto {
          if (lhs.axle_index != rhs.axle_index) {
            return lhs.axle_index < rhs.axle_index;
          }
          if (lhs.side != rhs.side) {
            return static_cast<uint32_t>(lhs.side)
              < static_cast<uint32_t>(rhs.side);
          }
          return lhs.node_index < rhs.node_index;
        });

      record.wheel_slice_offset = static_cast<uint32_t>(wheel_records.size());
      record.wheel_slice_count = static_cast<uint32_t>(sorted_wheels.size());
      for (const auto& wheel : sorted_wheels) {
        wheel_records.push_back(phys::VehicleWheelBindingRecord {
          .vehicle_node_index = record.node_index,
          .wheel_node_index = wheel.node_index,
          .axle_index = wheel.axle_index,
          .side = wheel.side,
          .backend_scalars = wheel.backend_scalars,
        });
      }
      vehicle_records.push_back(record);
    }
    auto aggregate_records = parsed.aggregates;
    for (const auto& record : rigid_records) {
      references.AddAsset(
        record.shape_asset_key, data::AssetType::kCollisionShape);
      references.AddAsset(
        record.material_asset_key, data::AssetType::kPhysicsMaterial);
    }
    for (const auto& record : collider_records) {
      references.AddAsset(
        record.shape_asset_key, data::AssetType::kCollisionShape);
      references.AddAsset(
        record.material_asset_key, data::AssetType::kPhysicsMaterial);
    }
    for (const auto& record : character_records) {
      references.AddAsset(
        record.shape_asset_key, data::AssetType::kCollisionShape);
      references.AddAsset(
        record.inner_shape_asset_key, data::AssetType::kCollisionShape);
    }
    for (const auto& source : soft_body_records) {
      references.AddPhysicsResource(source.record.topology_asset_key);
    }
    for (const auto& record : joint_records) {
      references.AddPhysicsResource(record.constraint_asset_key);
    }
    for (const auto& record : vehicle_records) {
      references.AddPhysicsResource(record.constraint_asset_key);
    }

    auto tables = std::vector<TableBlob> {};
    SortAndAppendTable(tables, phys::PhysicsBindingType::kRigidBody,
      rigid_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.node_index < rhs.node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kCollider,
      collider_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.node_index < rhs.node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kCharacter,
      character_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.node_index < rhs.node_index;
      });
    std::ranges::sort(
      soft_body_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.record.node_index < rhs.record.node_index;
      });
    if (const auto soft_body_table = MakeSoftBodyTableBlob(soft_body_records);
      soft_body_table.has_value()) {
      tables.push_back(*soft_body_table);
    }
    SortAndAppendTable(tables, phys::PhysicsBindingType::kJoint, joint_records,
      [](const auto& lhs, const auto& rhs) -> auto {
        if (lhs.node_index_a != rhs.node_index_a) {
          return lhs.node_index_a < rhs.node_index_a;
        }
        return lhs.node_index_b < rhs.node_index_b;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kVehicle,
      vehicle_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.node_index < rhs.node_index;
      });
    if (const auto wheel_table
      = MakeTableBlob(phys::PhysicsBindingType::kVehicleWheel, wheel_records);
      wheel_table.has_value()) {
      tables.push_back(*wheel_table);
    }
    SortAndAppendTable(tables, phys::PhysicsBindingType::kAggregate,
      aggregate_records, [](const auto& lhs, const auto& rhs) -> auto {
        return lhs.node_index < rhs.node_index;
      });

    return tables;
  }

  auto SerializeAndEmitPhysicsSidecar(
    const SidecarResolvedSceneState& scene_state, const ImportRequest& request,
    const std::vector<TableBlob>& tables, ImportSession& session,
    data::AssetReferences references) -> bool
  {
    auto sidecar_relpath
      = ReplaceSceneExtensionWithPhysics(scene_state.scene_descriptor_relpath);
    auto sidecar_virtual_path
      = ReplaceSceneExtensionWithPhysics(scene_state.scene_virtual_path);
    auto sidecar_name = std::filesystem::path(sidecar_relpath).stem().string();
    if (sidecar_name.empty()) {
      sidecar_name = "PhysicsScene";
    }

    auto serialize_error = std::string {};
    const auto descriptor_bytes
      = SerializePhysicsSceneAsset(scene_state, sidecar_name, tables,
        ResolveRequestedPhysicsBackend(request), serialize_error);
    if (!descriptor_bytes.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.descriptor_serialize_failed",
        std::move(serialize_error));
      return false;
    }

    const auto sidecar_key
      = oxygen::data::AssetKey::FromVirtualPath(sidecar_virtual_path);
    try {
      session.AssetEmitter().Emit(sidecar_key, data::AssetType::kPhysicsScene,
        sidecar_virtual_path, sidecar_relpath,
        std::span<const std::byte>(*descriptor_bytes), std::move(references));
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.descriptor_emit_failed",
        "Failed to emit physics sidecar descriptor: " + std::string(ex.what()));
      return false;
    }

    return true;
  }

} // namespace

PhysicsSidecarImportPipeline::PhysicsSidecarImportPipeline()
  : PhysicsSidecarImportPipeline(Config {})
{
}

PhysicsSidecarImportPipeline::PhysicsSidecarImportPipeline(Config config)
  : config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

PhysicsSidecarImportPipeline::~PhysicsSidecarImportPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "PhysicsSidecarImportPipeline destroyed with {} pending items",
      PendingCount());
  }
  input_channel_.Close();
  output_channel_.Close();
}

auto PhysicsSidecarImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(
    !started_, "PhysicsSidecarImportPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this] -> co::Co<> { co_await Worker(); });
  }
}

auto PhysicsSidecarImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  pending_.fetch_add(1, std::memory_order_acq_rel);
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto PhysicsSidecarImportPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto PhysicsSidecarImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .telemetry = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto PhysicsSidecarImportPipeline::Close() -> void { input_channel_.Close(); }

auto PhysicsSidecarImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto PhysicsSidecarImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto PhysicsSidecarImportPipeline::GetProgress() const noexcept
  -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto PhysicsSidecarImportPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_possible() && item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto process_start = std::chrono::steady_clock::now();
    auto result = WorkResult {
      .source_id = item.source_id,
      .telemetry = {},
      .success = false,
    };

    try {
      result.success = co_await Process(item);
    } catch (const std::exception& ex) {
      if (item.session != nullptr) {
        const auto& request = item.session->Request();
        AddDiagnostic(*item.session, request, ImportSeverity::kError,
          "physics.sidecar.pipeline_exception",
          std::string { "Unhandled physics sidecar pipeline exception: " }
            + ex.what());
      }
      result.success = false;
    }

    result.telemetry.cook_duration
      = MakeDuration(process_start, std::chrono::steady_clock::now());
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto PhysicsSidecarImportPipeline::Process(WorkItem& item) -> co::Co<bool>
{
  auto* const session = item.session.get();
  if (session == nullptr) {
    co_return false;
  }

  const auto& request = session->Request();
  if (!ValidatePhysicsSidecarRequest(*session, request) || !request.physics) {
    co_return false;
  }

  const auto& physics = *request.physics;
  const auto source_bytes = item.source_bytes.Bytes();
  auto source_text = std::string(source_bytes.size(), '\0');
  if (!source_bytes.empty()) {
    std::memcpy(source_text.data(), source_bytes.data(), source_bytes.size());
  }
  auto document_diagnostics = std::vector<ImportDiagnostic> {};
  auto parsed = PreparePhysicsSidecarDocument(
    source_text, request.source_path, document_diagnostics);
  for (auto& diagnostic : document_diagnostics) {
    session->AddDiagnostic(std::move(diagnostic));
  }
  if (!parsed.has_value()) {
    co_return false;
  }

  auto mounts = internal::LoadMountedCookedSources(
    *session, request, kPhysicsSidecarCodePrefix);
  if (session->HasErrors()) {
    co_return false;
  }

  auto resolved_scene_state
    = detail::ResolveTargetSceneState(*session, request, mounts,
      physics.target_scene_virtual_path, kPhysicsSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  if (!ResolveAndValidateBindings(
        *parsed, *resolved_scene_state, mounts, *session, request)) {
    co_return false;
  }

  AssetReferenceBuilder references;
  references.AddLogical(
    resolved_scene_state->scene_key, data::AssetType::kScene);
  const auto tables = BuildPhysicsSidecarTables(*parsed, references);
  // The mounts keep files of the cooked root open; the emission writes it.
  mounts.clear();
  if (!SerializeAndEmitPhysicsSidecar(*resolved_scene_state, request, tables,
        *session, std::move(references).Build())) {
    co_return false;
  }

  co_return true;
}

auto PhysicsSidecarImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  co_await output_channel_.Send(WorkResult {
    .source_id = std::move(item.source_id),
    .telemetry = {},
    .success = false,
  });
}

} // namespace oxygen::content::import
