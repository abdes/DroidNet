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
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Jolt/Core/Core.h>
#include <Jolt/Core/LinearCurve.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/ConstraintPart/SwingTwistConstraintPart.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/MotorSettings.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>
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
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/JoltShapeCook.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedMeshSurface.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {
namespace {

  namespace phys = data::pak::physics;
  using core::meta::physics::PhysicsBackend;
  using SidecarResolvedSceneState = detail::ResolvedSceneState;

  const auto kPhysicsSidecarResolverDiagnostics
    = detail::SidecarSceneResolverDiagnostics {
        .inflight_target_scene_ambiguous_code
        = import_codes::kPhysicsSidecarInflightTargetSceneAmbiguous,
        .target_scene_invalid_code
        = import_codes::kPhysicsSidecarTargetSceneInvalid,
        .target_scene_not_scene_code
        = import_codes::kPhysicsSidecarTargetSceneNotScene,
        .target_scene_read_failed_code
        = import_codes::kPhysicsSidecarTargetSceneReadFailed,
        .target_scene_virtual_path_invalid_code
        = import_codes::kPhysicsSidecarTargetSceneVirtualPathInvalid,
        .target_scene_missing_code
        = import_codes::kPhysicsSidecarTargetSceneMissing,
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

  struct BindingValidationContext final {
    observer_ptr<ImportSession> session;
    observer_ptr<const ImportRequest> request;
    std::span<const internal::MountedCookedSource> mounts;
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
    // Any mounted cooked root may provide what a binding names; the session
    // records the requirement on another mount when it finalizes.
    const auto found = internal::LookupReference(
      ctx.mounts, *ctx.request, *reference, expected_type);
    if (found.has_value()) {
      return found->key;
    }
    const auto& refused = found.error();
    switch (refused.reason) {
    case internal::AssetReferenceRefusal::kUnmounted:
      ctx.session->AddDiagnostic(internal::RefuseUnmountedReference(
        *ctx.request, *reference, location.to_string()));
      break;
    case internal::AssetReferenceRefusal::kMissing:
      AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
        std::string(codes.unresolved),
        "Reference was not found: "
          + internal::DescribeLookup(*reference, refused),
        location);
      break;
    case internal::AssetReferenceRefusal::kTypeMismatch:
      AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
        std::string(codes.wrong_type),
        "Reference names a "
          + std::string(data::to_string(
            refused.found_type.value_or(data::AssetType::kUnknown)))
          + " asset, expected a " + std::string(data::to_string(expected_type))
          + ": " + internal::DescribeLookup(*reference, refused),
        location);
      break;
    }
    return std::nullopt;
  }

  auto ValidateNodeIndex(const uint32_t node_index, const uint32_t node_count,
    ImportSession& session, const ImportRequest& request,
    const nlohmann::json::json_pointer& location) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnosticAtPath(session, request, ImportSeverity::kError,
      std::string(import_codes::kPhysicsSidecarNodeRefOutOfBounds),
      "Node index " + std::to_string(node_index)
        + " is out of bounds: the target scene has "
        + std::to_string(node_count) + " nodes",
      location);
    return false;
  }

  //! Refuses a binding whose authored `backend.target` is not `requested`:
  //! its backend scalars hold another backend's arm.
  auto CheckAuthoredBackend(const PhysicsBackend authored,
    const PhysicsBackend requested,
    const nlohmann::json::json_pointer& binding_path,
    BindingValidationContext& ctx) -> bool
  {
    if (authored == PhysicsBackend::kNone || authored == requested) {
      return true;
    }
    AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
      std::string(import_codes::kPhysicsSidecarBackendMismatch),
      "backend.target is " + std::string(BackendName(authored))
        + " but the import cooks for " + std::string(BackendName(requested)),
      binding_path / "backend" / "target");
    return false;
  }

  template <typename BindingSourceT>
  auto CheckAuthoredBackends(const std::vector<BindingSourceT>& bindings,
    const std::string_view category, const PhysicsBackend requested,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < bindings.size(); ++i) {
      (void)CheckAuthoredBackend(bindings.at(i).authored_backend, requested,
        nlohmann::json::json_pointer {} / "bindings" / std::string(category)
          / i,
        ctx);
    }
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

      const auto shape_key = ResolveAssetKeyForVirtualPath(ctx,
        binding.shape_ref, base_path / "shape_ref",
        data::AssetType::kCollisionShape,
        ReferenceCodes {
          .unresolved = import_codes::kPhysicsSidecarShapeRefUnresolved,
          .wrong_type = import_codes::kPhysicsSidecarShapeRefNotCollisionShape,
        });
      const auto material_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.material_ref,
          base_path / "material_ref", data::AssetType::kPhysicsMaterial,
          ReferenceCodes {
            .unresolved = import_codes::kPhysicsSidecarMaterialRefUnresolved,
            .wrong_type
            = import_codes::kPhysicsSidecarMaterialRefNotPhysicsMaterial,
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

      const auto shape_key = ResolveAssetKeyForVirtualPath(ctx,
        binding.shape_ref, base_path / "shape_ref",
        data::AssetType::kCollisionShape,
        ReferenceCodes {
          .unresolved = import_codes::kPhysicsSidecarShapeRefUnresolved,
          .wrong_type = import_codes::kPhysicsSidecarShapeRefNotCollisionShape,
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
            .unresolved = import_codes::kPhysicsSidecarInnerShapeRefUnresolved,
            .wrong_type
            = import_codes::kPhysicsSidecarInnerShapeRefNotCollisionShape,
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
        std::string(import_codes::kPhysicsSidecarBackendUnsupported),
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
            .unresolved = import_codes::kPhysicsSidecarSourceMeshRefUnresolved,
            .wrong_type = import_codes::kPhysicsSidecarSourceMeshRefNotGeometry,
          });
      if (!source_mesh_key.has_value()) {
        continue;
      }
      records.at(i).source_mesh_asset_key = *source_mesh_key;

      const auto& collision_mesh_ref = records.at(i).collision_mesh_ref;
      if (collision_mesh_ref) {
        (void)ResolveAssetKeyForVirtualPath(ctx, *collision_mesh_ref,
          base_path / "collision_mesh_ref", data::AssetType::kGeometry,
          ReferenceCodes {
            .unresolved
            = import_codes::kPhysicsSidecarCollisionMeshRefUnresolved,
            .wrong_type
            = import_codes::kPhysicsSidecarCollisionMeshRefNotGeometry,
          });
        if (requested_backend == PhysicsBackend::kJolt) {
          AddDiagnosticAtPath(*ctx.session, *ctx.request,
            ImportSeverity::kError,
            std::string(import_codes::kPhysicsSidecarCapabilityUnsupported),
            "soft_bodies.collision_mesh_ref has no jolt soft-body setting",
            base_path / "collision_mesh_ref");
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
        std::string(import_codes::kPhysicsSidecarBackendUnsupported),
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
        std::string(import_codes::kPhysicsSidecarBackendUnsupported),
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
            ImportSeverity::kError,
            std::string(import_codes::kPhysicsSidecarBackendMismatch),
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
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
          "Vehicle wheel bindings must resolve to at least two valid wheels",
          base_path / "wheels");
        wheel_records.resize(wheel_offset);
        continue;
      }

      if (wheel_offset > std::numeric_limits<uint32_t>::max()) {
        AddDiagnosticAtPath(*ctx.session, *ctx.request, ImportSeverity::kError,
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
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

  [[nodiscard]] auto JsonFloatOr(
    const nlohmann::json& obj, const char* key, const float fallback) -> float
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    return obj.at(key).get<float>();
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

  [[nodiscard]] auto ToJoltVec3(const std::array<float, 3>& value) -> JPH::Vec3
  {
    return { value[0], value[1], value[2] };
  }

  //! A joint frame in Jolt terms: its point and its primary and normal axes.
  struct JoltJointFrame final {
    JPH::RVec3 point;
    JPH::Vec3 axis_x;
    JPH::Vec3 axis_y;
  };

  [[nodiscard]] auto ToJoltJointFrame(const JointFrameSource& frame)
    -> JoltJointFrame
  {
    const auto& r = frame.rotation;
    const auto rotation = JPH::Quat(r[0], r[1], r[2], r[3]).Normalized();
    return {
      .point = JPH::RVec3(ToJoltVec3(frame.position)),
      .axis_x = rotation * JPH::Vec3::sAxisX(),
      .axis_y = rotation * JPH::Vec3::sAxisY(),
    };
  }

  [[nodiscard]] auto ToJoltConstraintSpace(const JointConstraintSpace space)
    -> JPH::EConstraintSpace
  {
    return space == JointConstraintSpace::kLocal
      ? JPH::EConstraintSpace::LocalToBodyCOM
      : JPH::EConstraintSpace::WorldSpace;
  }

  constexpr auto kJointAxisTx = size_t { 0 };
  constexpr auto kJointAxisRx = size_t { 3 };
  constexpr auto kJointAxisRy = size_t { 4 };
  constexpr auto kJointTranslationAxes = size_t { 3 };

  //! The value of `axis` in `values`, or zero when they are not authored. A
  //! zero keeps the backend's default.
  [[nodiscard]] auto AxisValue(
    const std::optional<JointAxisValues>& values, const size_t axis) -> float
  {
    return values.has_value() ? values->at(axis) : 0.0F;
  }

  //! The limit spring of `axis`; none (hard limits) without a stiffness or a
  //! damping.
  auto ApplyLimitSpring(const JointSettingsSource& settings, const size_t axis,
    JPH::SpringSettings& out) -> void
  {
    const auto stiffness = AxisValue(settings.spring_stiffnesses, axis);
    const auto damping = AxisValue(settings.spring_dampings, axis);
    const auto has_spring = stiffness > 0.0F || damping > 0.0F;
    if (has_spring) {
      out = JPH::SpringSettings(
        JPH::ESpringMode::StiffnessAndDamping, stiffness, damping);
    }
  }

  //! The motor of `axis`: its drive spring and its force or torque limit,
  //! each left at the backend's default when zero.
  auto ApplyMotor(const JointSettingsSource& settings, const size_t axis,
    JPH::MotorSettings& out) -> void
  {
    const auto frequency = AxisValue(settings.motor_drive_frequencies, axis);
    const auto damping = AxisValue(settings.motor_damping_ratios, axis);
    const auto has_drive = frequency > 0.0F || damping > 0.0F;
    if (has_drive) {
      out.mSpringSettings = JPH::SpringSettings(
        JPH::ESpringMode::FrequencyAndDamping, frequency, damping);
    }
    const auto force = AxisValue(settings.motor_max_forces, axis);
    const auto torque = AxisValue(settings.motor_max_torques, axis);
    const auto translation = axis < kJointTranslationAxes;
    if (translation && force > 0.0F) {
      out.SetForceLimit(force);
    }
    if (!translation && torque > 0.0F) {
      out.SetTorqueLimit(torque);
    }
  }

  //! The settings every constraint type shares.
  auto ApplyCommonJointSettings(
    const JointBindingSource& source, JPH::ConstraintSettings& out) -> void
  {
    out.mConstraintPriority = source.settings.priority;
    out.mNumVelocityStepsOverride
      = source.record.backend_scalars.jolt.num_velocity_steps_override;
    out.mNumPositionStepsOverride
      = source.record.backend_scalars.jolt.num_position_steps_override;
  }

  auto ConfigureJoltFixed(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::FixedConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mAutoDetectPoint = false;
    out.mPoint1 = a.point;
    out.mAxisX1 = a.axis_x;
    out.mAxisY1 = a.axis_y;
    out.mPoint2 = b.point;
    out.mAxisX2 = b.axis_x;
    out.mAxisY2 = b.axis_y;
  }

  auto ConfigureJoltPoint(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::PointConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mPoint1 = a.point;
    out.mPoint2 = b.point;
  }

  auto ConfigureJoltHinge(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::HingeConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mPoint1 = a.point;
    out.mHingeAxis1 = a.axis_x;
    out.mNormalAxis1 = a.axis_y;
    out.mPoint2 = b.point;
    out.mHingeAxis2 = b.axis_x;
    out.mNormalAxis2 = b.axis_y;
    if (settings.limits_lower.has_value()
      && settings.limits_upper.has_value()) {
      out.mLimitsMin = settings.limits_lower->at(kJointAxisRx);
      out.mLimitsMax = settings.limits_upper->at(kJointAxisRx);
    }
    ApplyLimitSpring(settings, kJointAxisRx, out.mLimitsSpringSettings);
    ApplyMotor(settings, kJointAxisRx, out.mMotorSettings);
  }

  auto ConfigureJoltSlider(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::SliderConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mAutoDetectPoint = false;
    out.mPoint1 = a.point;
    out.mSliderAxis1 = a.axis_x;
    out.mNormalAxis1 = a.axis_y;
    out.mPoint2 = b.point;
    out.mSliderAxis2 = b.axis_x;
    out.mNormalAxis2 = b.axis_y;
    if (settings.limits_lower.has_value()
      && settings.limits_upper.has_value()) {
      out.mLimitsMin = settings.limits_lower->at(kJointAxisTx);
      out.mLimitsMax = settings.limits_upper->at(kJointAxisTx);
    }
    ApplyLimitSpring(settings, kJointAxisTx, out.mLimitsSpringSettings);
    ApplyMotor(settings, kJointAxisTx, out.mMotorSettings);
  }

  auto ConfigureJoltDistance(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::DistanceConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mPoint1 = a.point;
    out.mPoint2 = b.point;
    if (settings.limits_lower.has_value()
      && settings.limits_upper.has_value()) {
      out.mMinDistance = settings.limits_lower->at(kJointAxisTx);
      out.mMaxDistance = settings.limits_upper->at(kJointAxisTx);
    }
    ApplyLimitSpring(settings, kJointAxisTx, out.mLimitsSpringSettings);
  }

  auto ConfigureJoltCone(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::ConeConstraintSettings& out) -> void
  {
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mPoint1 = a.point;
    out.mTwistAxis1 = a.axis_x;
    out.mPoint2 = b.point;
    out.mTwistAxis2 = b.axis_x;
    if (settings.limits_upper.has_value()) {
      out.mHalfConeAngle = settings.limits_upper->at(kJointAxisRy);
    }
  }

  auto ConfigureJoltSixDof(const JointSettingsSource& settings,
    const JoltJointFrame& a, const JoltJointFrame& b,
    JPH::SixDOFConstraintSettings& out) -> void
  {
    using EAxis = JPH::SixDOFConstraintSettings::EAxis;
    out.mSpace = ToJoltConstraintSpace(settings.space);
    out.mPosition1 = a.point;
    out.mAxisX1 = a.axis_x;
    out.mAxisY1 = a.axis_y;
    out.mPosition2 = b.point;
    out.mAxisX2 = b.axis_x;
    out.mAxisY2 = b.axis_y;

    const auto has_limits
      = settings.limits_lower.has_value() && settings.limits_upper.has_value();
    for (size_t axis = 0; axis < JPH::SixDOFConstraintSettings::EAxis::Num;
      ++axis) {
      const auto jolt_axis = static_cast<EAxis>(axis);
      const auto default_mode
        = has_limits ? JointAxisMode::kLimited : JointAxisMode::kFree;
      const auto mode = settings.axis_modes.has_value()
        ? settings.axis_modes->at(axis)
        : default_mode;
      switch (mode) {
      case JointAxisMode::kFree:
        out.MakeFreeAxis(jolt_axis);
        break;
      case JointAxisMode::kLocked:
        out.MakeFixedAxis(jolt_axis);
        break;
      case JointAxisMode::kLimited:
        // The preparation proved a limited axis has both limits.
        out.SetLimitedAxis(jolt_axis, AxisValue(settings.limits_lower, axis),
          AxisValue(settings.limits_upper, axis));
        break;
      }
      if (axis < kJointTranslationAxes) {
        ApplyLimitSpring(settings, axis,
          base::CheckedAt(std::span { out.mLimitsSpringSettings }, axis));
      }
      ApplyMotor(settings, axis,
        base::CheckedAt(std::span { out.mMotorSettings }, axis));
    }

    // A cone swing is symmetric about each axis; an asymmetric one is a
    // pyramid.
    const auto symmetric_swing = !has_limits
      || std::ranges::all_of(std::array { EAxis::RotationY, EAxis::RotationZ },
        [&](const EAxis axis) -> bool {
          return base::CheckedAt(std::span { out.mLimitMin }, axis)
            == -base::CheckedAt(std::span { out.mLimitMax }, axis);
        });
    out.mSwingType
      = symmetric_swing ? JPH::ESwingType::Cone : JPH::ESwingType::Pyramid;
  }

  //! Serializes the `SettingsT` constraint `configure` sets up, with the
  //! settings every joint shares.
  template <typename SettingsT, typename ConfigureFn>
  auto SerializeJoltJoint(const JointBindingSource& source,
    const ConfigureFn& configure, std::vector<std::byte>& out_blob) -> bool
  {
    const auto a = ToJoltJointFrame(source.settings.frame_a);
    const auto b = ToJoltJointFrame(source.settings.frame_b);
    auto settings = SettingsT {};
    configure(source.settings, a, b, settings);
    ApplyCommonJointSettings(source, settings);
    return SerializeJoltConstraintSettings(settings, out_blob);
  }

  //! Cooks the Jolt constraint of a joint the preparation validated.
  auto CookJoltJointBlob(const JointBindingSource& source,
    std::vector<std::byte>& out_blob, std::string& error) -> bool
  {
    internal::EnsureJoltAllocatorReady();
    switch (source.settings.type) {
    case JointConstraintType::kFixed:
      return SerializeJoltJoint<JPH::FixedConstraintSettings>(
        source, ConfigureJoltFixed, out_blob);
    case JointConstraintType::kPoint:
      return SerializeJoltJoint<JPH::PointConstraintSettings>(
        source, ConfigureJoltPoint, out_blob);
    case JointConstraintType::kHinge:
      return SerializeJoltJoint<JPH::HingeConstraintSettings>(
        source, ConfigureJoltHinge, out_blob);
    case JointConstraintType::kSlider:
      return SerializeJoltJoint<JPH::SliderConstraintSettings>(
        source, ConfigureJoltSlider, out_blob);
    case JointConstraintType::kDistance:
      return SerializeJoltJoint<JPH::DistanceConstraintSettings>(
        source, ConfigureJoltDistance, out_blob);
    case JointConstraintType::kCone:
      return SerializeJoltJoint<JPH::ConeConstraintSettings>(
        source, ConfigureJoltCone, out_blob);
    case JointConstraintType::kSixDof:
      return SerializeJoltJoint<JPH::SixDOFConstraintSettings>(
        source, ConfigureJoltSixDof, out_blob);
    }
    error = "joint constraint_type has no jolt constraint";
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
    std::string code {
      import_codes::kPhysicsSidecarResourceSerializeFailed,
    };
    std::string reason;
  };

  //! Reads the source mesh surface of a soft body from the cooked root that
  //! provides it.
  auto BuildSoftBodySurfaceMesh(const SoftBodyBindingSource& source,
    const cooked::CookedContent& source_content, SoftBodySurfaceMesh& out,
    std::string& error) -> bool
  {
    auto surface = internal::ReadCookedMeshSurface(
      source_content, source.source_mesh_asset_key);
    if (!surface) {
      error = "soft-body source mesh: " + surface.error();
      return false;
    }
    out.vertices.clear();
    out.vertices.reserve(surface->positions.size());
    for (const auto& p : surface->positions) {
      out.vertices.emplace_back(p[0], p[1], p[2]);
    }
    out.faces.clear();
    out.faces.reserve(surface->triangles.size());
    for (const auto& t : surface->triangles) {
      out.faces.emplace_back(t[0], t[1], t[2], 0U);
    }
    return true;
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
          .code = std::string(
            import_codes::kPhysicsSidecarSoftBodySurfaceNotManifold),
          .reason = "a face collapses once its vertices are welded",
        };
        return false;
      }
      for (size_t k = 0; k < 3U; ++k) {
        const auto from = face.mVertex[k];
        const auto to = face.mVertex[(k + 1U) % 3U];
        if (++directed[{ from, to }] > 1U) {
          error = {
            .code = std::string(
              import_codes::kPhysicsSidecarSoftBodySurfaceNotManifold),
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
            .code = std::string(
              import_codes::kPhysicsSidecarSoftBodySurfaceNotClosed),
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
        .code
        = std::string(import_codes::kPhysicsSidecarSoftBodySurfaceDisconnected),
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
          .code = std::string(import_codes::kPhysicsSidecarSoftBodyPinInvalid),
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
    const cooked::CookedContent& source_content,
    std::vector<std::byte>& out_blob, SoftBodyCookError& error) -> bool
  {
    internal::EnsureJoltAllocatorReady();

    auto surface_mesh = SoftBodySurfaceMesh {};
    if (!BuildSoftBodySurfaceMesh(
          source, source_content, surface_mesh, error.reason)) {
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
    internal::EnsureJoltAllocatorReady();
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
    const cooked::CookedContent& source_content,
    std::vector<std::byte>& out_blob, SoftBodyCookError& error) -> bool
  {
    switch (source.record.topology_format) {
    case phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary:
      return CookJoltSoftBodyBlob(source, source_content, out_blob, error);
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
        std::string(import_codes::kPhysicsSidecarResourceEmitFailed),
        "Failed emitting backend-cooked physics payload: "
          + std::string(ex.what()),
        location);
      return false;
    }
  }

  //! The backend blobs of a document's bindings, in binding order.
  struct CookedBindingBlobs final {
    std::vector<std::vector<std::byte>> soft_bodies;
    std::vector<std::vector<std::byte>> joints;
    std::vector<std::vector<std::byte>> vehicles;
  };

  //! Cooks the backend blob of every soft body, joint and vehicle without
  //! emitting any; reports each failure.
  auto CookBindingBlobs(const PhysicsSidecarDocument& parsed,
    ImportSession& session, const ImportRequest& request,
    const std::span<const internal::MountedCookedSource> mounts)
    -> std::optional<CookedBindingBlobs>
  {
    auto blobs = CookedBindingBlobs {};
    for (size_t i = 0; i < parsed.soft_bodies.size(); ++i) {
      const auto& source = parsed.soft_bodies.at(i);
      // The resolution found the source mesh in one of the mounted roots.
      const auto* const source_content
        = internal::FindListingContent(mounts, source.source_mesh_asset_key);
      CHECK_NOTNULL_F(source_content, "resolved source mesh is not mounted");
      auto blob = std::vector<std::byte> {};
      auto cook_error = SoftBodyCookError {};
      if (!CookSoftBodyTopologyBlob(
            source, *source_content, blob, cook_error)) {
        LOG_F(ERROR,
          "PhysicsSidecarImportPipeline: soft-body topology cooking failed "
          "(source_mesh_ref='{}' code='{}' reason='{}')",
          source.source_mesh_ref, cook_error.code, cook_error.reason);
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(cook_error.code),
          "Failed to cook soft-body topology resource: " + cook_error.reason,
          nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i);
      }
      blobs.soft_bodies.push_back(std::move(blob));
    }
    for (size_t i = 0; i < parsed.joints.size(); ++i) {
      auto blob = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookJointConstraintBlob(parsed.joints.at(i), blob, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(import_codes::kPhysicsSidecarResourceSerializeFailed),
          "Failed to cook joint constraint resource: " + cook_error,
          nlohmann::json::json_pointer {} / "bindings" / "joints" / i);
      }
      blobs.joints.push_back(std::move(blob));
    }
    for (size_t i = 0; i < parsed.vehicles.size(); ++i) {
      auto blob = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookVehicleConstraintBlob(parsed.vehicles.at(i), blob, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(import_codes::kPhysicsSidecarResourceSerializeFailed),
          "Failed to cook vehicle constraint resource: " + cook_error,
          nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i);
      }
      blobs.vehicles.push_back(std::move(blob));
    }
    if (session.HasErrors()) {
      return std::nullopt;
    }
    return blobs;
  }

  //! Emits the cooked blobs and records each resource key in its binding.
  auto EmitBindingBlobs(PhysicsSidecarDocument& parsed,
    const CookedBindingBlobs& blobs, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    for (size_t i = 0; i < parsed.soft_bodies.size(); ++i) {
      auto& record = parsed.soft_bodies.at(i).record;
      (void)EmitBindingBlob(session, request,
        nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i,
        blobs.soft_bodies.at(i), record.topology_format,
        record.topology_asset_key);
    }
    for (size_t i = 0; i < parsed.joints.size(); ++i) {
      auto& source = parsed.joints.at(i);
      (void)EmitBindingBlob(session, request,
        nlohmann::json::json_pointer {} / "bindings" / "joints" / i,
        blobs.joints.at(i), source.constraint_format,
        source.record.constraint_asset_key);
    }
    for (size_t i = 0; i < parsed.vehicles.size(); ++i) {
      auto& source = parsed.vehicles.at(i);
      (void)EmitBindingBlob(session, request,
        nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i,
        blobs.vehicles.at(i), source.constraint_format,
        source.record.constraint_asset_key);
    }
    return !session.HasErrors();
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
        std::string(import_codes::kPhysicsRequestInvalidImportKind),
        "Physics sidecar import requires ImportRequest::physics payload");
      return false;
    }
    if (request.physics->target_scene_virtual_path.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kPhysicsRequestTargetSceneVirtualPathMissing),
        "Physics sidecar import requires target_scene_virtual_path");
      return false;
    }
    if (!internal::IsCanonicalVirtualPath(
          request.physics->target_scene_virtual_path)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kPhysicsSidecarTargetSceneVirtualPathInvalid),
        "Target scene virtual path must be canonical");
      return false;
    }
    // Only Jolt has a runtime; PhysX cooking waits for its backend
    // (abdes/DroidNet#133).
    const auto backend = ResolveRequestedPhysicsBackend(request);
    if (backend != PhysicsBackend::kJolt) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kPhysicsSidecarBackendUnsupported),
        "Physics sidecars cook for jolt only; the request asks for "
          + std::string(BackendName(backend)));
      return false;
    }
    return true;
  }

  auto ResolveAndValidateBindings(PhysicsSidecarDocument& parsed,
    const SidecarResolvedSceneState& resolved_scene_state,
    std::span<const internal::MountedCookedSource> mounts,
    ImportSession& session, const ImportRequest& request) -> bool
  {
    const auto requested_backend = ResolveRequestedPhysicsBackend(request);
    auto validation_ctx = BindingValidationContext {
      .session = observer_ptr { &session },
      .request = observer_ptr { &request },
      .mounts = mounts,
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
    CheckAuthoredBackends(
      parsed.rigid_bodies, "rigid_bodies", requested_backend, validation_ctx);
    CheckAuthoredBackends(
      parsed.characters, "characters", requested_backend, validation_ctx);
    CheckAuthoredBackends(
      parsed.soft_bodies, "soft_bodies", requested_backend, validation_ctx);
    CheckAuthoredBackends(
      parsed.joints, "joints", requested_backend, validation_ctx);
    return !session.HasErrors();
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
        std::string(import_codes::kPhysicsSidecarDescriptorSerializeFailed),
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
        std::string(import_codes::kPhysicsSidecarDescriptorEmitFailed),
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
          std::string(import_codes::kPhysicsSidecarPipelineException),
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
    *session, request, import_codes::kPhysicsSidecarIndexLoadFailed);
  if (session->HasErrors()) {
    co_return false;
  }

  auto resolved_scene_state
    = detail::ResolveTargetSceneState(*session, request, mounts,
      physics.target_scene_virtual_path, kPhysicsSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  if (detail::FindSceneContent(mounts, resolved_scene_state->scene_key)
    == nullptr) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      std::string(import_codes::kPhysicsSidecarTargetSceneMissing),
      "Resolved target scene key is not present in cooked scene context");
    co_return false;
  }
  if (!ResolveAndValidateBindings(
        *parsed, *resolved_scene_state, mounts, *session, request)) {
    co_return false;
  }

  // Every blob cooks before any is emitted, so a failing binding emits none.
  const auto blobs = CookBindingBlobs(*parsed, *session, request, mounts);
  if (!blobs.has_value()
    || !EmitBindingBlobs(*parsed, *blobs, *session, request)) {
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
