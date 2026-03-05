//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {
namespace {

  namespace phys = data::pak::physics;
  namespace lc = oxygen::content::lc;
  using SidecarCookedInspectionContext = detail::CookedInspectionContext;
  using SidecarResolvedSceneState = detail::ResolvedSceneState;

  const auto kPhysicsSidecarResolverDiagnostics
    = detail::SidecarSceneResolverDiagnostics {
        .index_load_failed_code = "physics.sidecar.index_load_failed",
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
    std::string object_path) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  constexpr size_t kMaxSchemaDiagnostics = 12;

  auto PhysicsSchemaValidator() -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator = [] {
      auto out = nlohmann::json_schema::json_validator {};
      out.set_root_schema(nlohmann::json::parse(kPhysicsSidecarSchema));
      return out;
    }();
    return validator;
  }

  auto ValidatePhysicsSchema(const nlohmann::json& doc, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    constexpr auto kSchemaDiagConfig
      = internal::JsonSchemaValidationDiagnosticConfig {
          .validation_failed_code = "physics.sidecar.schema_validation_failed",
          .validation_failed_prefix = "Schema validation failed: ",
          .validation_overflow_prefix = "Schema validation reported ",
          .validator_failure_code = "physics.sidecar.schema_validator_failure",
          .validator_failure_prefix
          = "Physics schema validation engine failure: ",
          .max_issues = kMaxSchemaDiagnostics,
        };

    return internal::ValidateJsonSchemaWithDiagnostics(PhysicsSchemaValidator(),
      doc, kSchemaDiagConfig,
      [&](const std::string_view code, std::string message,
        std::string object_path) {
        if (object_path.empty()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            std::string(code), std::move(message));
          return;
        }
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(code), std::move(message), std::move(object_path));
      });
  }

  auto ReplaceSceneExtensionWithPhysics(std::string value) -> std::string
  {
    constexpr auto kSceneSuffix = LooseCookedLayout::kSceneDescriptorExtension;
    constexpr auto kPhysicsSuffix
      = LooseCookedLayout::kPhysicsSceneDescriptorExtension;

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

  [[nodiscard]] auto NormalizeRelPath(std::string relpath) -> std::string
  {
    std::replace(relpath.begin(), relpath.end(), '\\', '/');
    return relpath;
  }

  auto PatchContentHash(std::vector<std::byte>& bytes, const uint64_t hash)
    -> void
  {
    constexpr auto kOffset
      = offsetof(data::pak::core::AssetHeader, content_hash);
    if (bytes.size() < kOffset + sizeof(hash)) {
      return;
    }
    std::memcpy(bytes.data() + kOffset, &hash, sizeof(hash));
  }

  struct RigidBodyBindingSource final {
    phys::RigidBodyBindingRecord record {};
    std::string shape_ref;
    std::string material_ref;
  };

  struct ColliderBindingSource final {
    phys::ColliderBindingRecord record {};
    std::string shape_ref;
    std::string material_ref;
  };

  struct CharacterBindingSource final {
    phys::CharacterBindingRecord record {};
    std::string shape_ref;
    std::optional<std::string> inner_shape_ref;
  };

  struct SoftBodyBindingSource final {
    phys::SoftBodyBindingRecord record {};
    std::string source_mesh_ref;
    std::optional<std::string> collision_mesh_ref;
    std::vector<uint32_t> pinned_vertices;
    std::vector<uint32_t> kinematic_vertices;
  };

  struct VehicleWheelSource final {
    uint32_t node_index { 0 };
    uint16_t axle_index { 0 };
    phys::VehicleWheelSide side { phys::VehicleWheelSide::kLeft };
    phys::VehicleWheelBackendScalars backend_scalars {};
  };

  struct JointBindingSource final {
    phys::JointBindingRecord record {};
    std::optional<std::string> constraint_ref;
  };

  struct VehicleBindingSource final {
    phys::VehicleBindingRecord record {};
    std::optional<std::string> constraint_ref;
    std::vector<VehicleWheelSource> wheels;
  };

  struct PhysicsSidecarDocument final {
    std::vector<RigidBodyBindingSource> rigid_bodies;
    std::vector<ColliderBindingSource> colliders;
    std::vector<CharacterBindingSource> characters;
    std::vector<SoftBodyBindingSource> soft_bodies;
    std::vector<JointBindingSource> joints;
    std::vector<VehicleBindingSource> vehicles;
    std::vector<phys::VehicleWheelBindingRecord> vehicle_wheels;
    std::vector<phys::AggregateBindingRecord> aggregates;
  };

  auto ReadRequiredString(const nlohmann::json& obj, const char* field,
    std::string& out, std::string& error) -> bool
  {
    (void)error;
    out = obj.at(field).get<std::string>();
    return true;
  }

  auto ReadRequiredUInt32(const nlohmann::json& obj, const char* field,
    uint32_t& out, std::string& error) -> bool
  {
    (void)error;
    out = obj.at(field).get<uint32_t>();
    return true;
  }

  auto ReadRequiredFloat(const nlohmann::json& obj, const char* field,
    float& out, std::string& error) -> bool
  {
    (void)error;
    out = obj.at(field).get<float>();
    return true;
  }

  auto ReadRequiredFloat3(const nlohmann::json& obj, const char* field,
    float (&out)[3], std::string& error) -> bool
  {
    (void)error;
    const auto values = obj.at(field).get<std::array<float, 3>>();
    out[0] = values[0];
    out[1] = values[1];
    out[2] = values[2];
    return true;
  }

  auto ReadRequiredUInt32Array(const nlohmann::json& obj, const char* field,
    std::vector<uint32_t>& out, std::string& error) -> bool
  {
    (void)error;
    const auto& values = obj.at(field);
    out.clear();
    out.reserve(values.size());
    for (const auto& value : values) {
      out.push_back(value.get<uint32_t>());
    }
    return true;
  }

  auto ReadJointNodeIndexB(
    const nlohmann::json& obj, uint32_t& out, std::string& error) -> bool
  {
    if (!obj.contains("node_index_b")) {
      error = "Missing required field: node_index_b";
      return false;
    }
    const auto& value = obj.at("node_index_b");
    if (value.is_null()) {
      out = phys::kWorldAttachmentNodeIndex;
      return true;
    }
    if (value.is_string()) {
      const auto token = value.get<std::string>();
      if (token == "world") {
        out = phys::kWorldAttachmentNodeIndex;
        return true;
      }
      error = "node_index_b string value must be \"world\"";
      return false;
    }
    if (value.is_number_unsigned()) {
      const auto raw = value.get<uint64_t>();
      if (raw > (std::numeric_limits<uint32_t>::max)()) {
        error = "node_index_b is out of uint32 range";
        return false;
      }
      out = static_cast<uint32_t>(raw);
      return true;
    }
    error = "node_index_b must be uint32, null, or \"world\"";
    return false;
  }

  auto ReadOptionalUInt32(const nlohmann::json& obj, const char* field,
    uint32_t& out, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    out = obj.at(field).get<uint32_t>();
    return true;
  }

  auto ReadOptionalUInt16(const nlohmann::json& obj, const char* field,
    uint16_t& out, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    out = obj.at(field).get<uint16_t>();
    return true;
  }

  auto ReadOptionalUInt8(const nlohmann::json& obj, const char* field,
    uint8_t& out, std::string& error) -> bool
  {
    if (!obj.contains(field)) {
      return true;
    }
    const auto raw = obj.at(field).get<uint32_t>();
    if (raw > (std::numeric_limits<uint8_t>::max)()) {
      error = std::string("Field '") + field + "' exceeds uint8 range";
      return false;
    }
    out = static_cast<uint8_t>(raw);
    return true;
  }

  auto ReadOptionalFloat(const nlohmann::json& obj, const char* field,
    float& out, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    out = obj.at(field).get<float>();
    return true;
  }

  auto ReadOptionalBool(const nlohmann::json& obj, const char* field, bool& out,
    std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    out = obj.at(field).get<bool>();
    return true;
  }

  auto ReadOptionalString(const nlohmann::json& obj, const char* field,
    std::optional<std::string>& out, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    out = obj.at(field).get<std::string>();
    return true;
  }

  auto ReadOptionalFloat3(const nlohmann::json& obj, const char* field,
    float (&out)[3], uint32_t& has_override, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    const auto values = obj.at(field).get<std::array<float, 3>>();
    out[0] = values[0];
    out[1] = values[1];
    out[2] = values[2];
    has_override = 1U;
    return true;
  }

  auto ParseBodyType(const nlohmann::json& obj, phys::PhysicsBodyType& out,
    std::string& error) -> bool
  {
    if (!obj.contains("body_type")) {
      return true;
    }
    const auto value = obj.at("body_type").get<std::string>();
    if (value == "static") {
      out = phys::PhysicsBodyType::kStatic;
      return true;
    }
    if (value == "dynamic") {
      out = phys::PhysicsBodyType::kDynamic;
      return true;
    }
    if (value == "kinematic") {
      out = phys::PhysicsBodyType::kKinematic;
      return true;
    }
    error = "Field 'body_type' has unsupported value";
    return false;
  }

  auto ParseMotionQuality(const nlohmann::json& obj,
    phys::PhysicsMotionQuality& out, std::string& error) -> bool
  {
    if (!obj.contains("motion_quality")) {
      return true;
    }
    const auto value = obj.at("motion_quality").get<std::string>();
    if (value == "discrete") {
      out = phys::PhysicsMotionQuality::kDiscrete;
      return true;
    }
    if (value == "linear_cast") {
      out = phys::PhysicsMotionQuality::kLinearCast;
      return true;
    }
    error = "Field 'motion_quality' has unsupported value";
    return false;
  }

  auto ParseTetherMode(const nlohmann::json& obj, phys::SoftBodyTetherMode& out,
    std::string& error) -> bool
  {
    if (!obj.contains("tether_mode")) {
      return true;
    }
    const auto value = obj.at("tether_mode").get<std::string>();
    if (value == "none") {
      out = phys::SoftBodyTetherMode::kNone;
      return true;
    }
    if (value == "euclidean") {
      out = phys::SoftBodyTetherMode::kEuclidean;
      return true;
    }
    if (value == "geodesic") {
      out = phys::SoftBodyTetherMode::kGeodesic;
      return true;
    }
    error = "Field 'tether_mode' has unsupported value";
    return false;
  }

  auto ParseWheelSide(const nlohmann::json& obj, phys::VehicleWheelSide& out,
    std::string& error) -> bool
  {
    const auto value = obj.at("side").get<std::string>();
    if (value == "left") {
      out = phys::VehicleWheelSide::kLeft;
      return true;
    }
    if (value == "right") {
      out = phys::VehicleWheelSide::kRight;
      return true;
    }
    error = "Field 'side' has unsupported value";
    return false;
  }

  auto ParseAggregateAuthority(const nlohmann::json& obj,
    phys::AggregateAuthority& out, std::string& error) -> bool
  {
    if (!obj.contains("authority")) {
      return true;
    }
    const auto value = obj.at("authority").get<std::string>();
    if (value == "simulation") {
      out = phys::AggregateAuthority::kSimulation;
      return true;
    }
    if (value == "command") {
      out = phys::AggregateAuthority::kCommand;
      return true;
    }
    error = "Field 'authority' has unsupported value";
    return false;
  }

  auto ParseAllowedDofFlags(
    const nlohmann::json& obj, uint32_t& out, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains("allowed_dof")) {
      return true;
    }
    const auto& dof = obj.at("allowed_dof");
    constexpr uint32_t kTranslateXBit = 1U << 0U;
    constexpr uint32_t kTranslateYBit = 1U << 1U;
    constexpr uint32_t kTranslateZBit = 1U << 2U;
    constexpr uint32_t kRotateXBit = 1U << 3U;
    constexpr uint32_t kRotateYBit = 1U << 4U;
    constexpr uint32_t kRotateZBit = 1U << 5U;
    out = 0U;
    if (dof.value("translate_x", false)) {
      out |= kTranslateXBit;
    }
    if (dof.value("translate_y", false)) {
      out |= kTranslateYBit;
    }
    if (dof.value("translate_z", false)) {
      out |= kTranslateZBit;
    }
    if (dof.value("rotate_x", false)) {
      out |= kRotateXBit;
    }
    if (dof.value("rotate_y", false)) {
      out |= kRotateYBit;
    }
    if (dof.value("rotate_z", false)) {
      out |= kRotateZBit;
    }
    return true;
  }

  auto ParseRigidBodyBackend(const nlohmann::json& obj,
    phys::RigidBodyBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    const auto target = backend.at("target").get<std::string>();
    if (target == "jolt") {
      return ReadOptionalUInt8(backend, "num_velocity_steps_override",
               out.backend_scalars.jolt.num_velocity_steps_override, error)
        && ReadOptionalUInt8(backend, "num_position_steps_override",
          out.backend_scalars.jolt.num_position_steps_override, error);
    }
    if (target == "physx") {
      return ReadOptionalUInt8(backend, "min_velocity_iters",
               out.backend_scalars.physx.min_velocity_iters, error)
        && ReadOptionalUInt8(backend, "min_position_iters",
          out.backend_scalars.physx.min_position_iters, error)
        && ReadOptionalFloat(backend, "max_contact_impulse",
          out.backend_scalars.physx.max_contact_impulse, error)
        && ReadOptionalFloat(backend, "contact_report_threshold",
          out.backend_scalars.physx.contact_report_threshold, error);
    }
    error = "Field 'backend.target' has unsupported value";
    return false;
  }

  auto ParseCharacterBackend(const nlohmann::json& obj,
    phys::CharacterBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    const auto target = backend.at("target").get<std::string>();
    if (target == "jolt") {
      return ReadOptionalFloat(backend, "penetration_recovery_speed",
               out.backend_scalars.jolt.penetration_recovery_speed, error)
        && ReadOptionalUInt32(
          backend, "max_num_hits", out.backend_scalars.jolt.max_num_hits, error)
        && ReadOptionalFloat(backend, "hit_reduction_cos_max_angle",
          out.backend_scalars.jolt.hit_reduction_cos_max_angle, error);
    }
    if (target == "physx") {
      return ReadOptionalFloat(backend, "contact_offset",
        out.backend_scalars.physx.contact_offset, error);
    }
    error = "Field 'backend.target' has unsupported value";
    return false;
  }

  auto ParseSoftBodyBackend(const nlohmann::json& obj,
    phys::SoftBodyBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    const auto target = backend.at("target").get<std::string>();
    if (target == "jolt") {
      out.topology_format
        = phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary;
      if (!ReadOptionalUInt32(backend, "velocity_iteration_count",
            out.backend_scalars.jolt.num_velocity_steps, error)) {
        return false;
      }
      out.backend_scalars.jolt.num_position_steps = out.solver_iteration_count;
      out.backend_scalars.jolt.gravity_factor = 1.0F;
      return true;
    }
    if (target == "physx") {
      out.topology_format
        = phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary;
      return ReadOptionalFloat(backend, "youngs_modulus",
               out.backend_scalars.physx.youngs_modulus, error)
        && ReadOptionalFloat(
          backend, "poisson_ratio", out.backend_scalars.physx.poissons, error)
        && ReadOptionalFloat(backend, "dynamic_friction",
          out.backend_scalars.physx.dynamic_friction, error);
    }
    error = "Field 'backend.target' has unsupported value";
    return false;
  }

  auto ParseJointBackend(const nlohmann::json& obj,
    phys::JointBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    const auto target = backend.at("target").get<std::string>();
    if (target == "jolt") {
      return ReadOptionalUInt8(backend, "num_velocity_steps_override",
               out.backend_scalars.jolt.num_velocity_steps_override, error)
        && ReadOptionalUInt8(backend, "num_position_steps_override",
          out.backend_scalars.jolt.num_position_steps_override, error);
    }
    if (target == "physx") {
      return ReadOptionalFloat(backend, "inv_mass_scale0",
               out.backend_scalars.physx.inv_mass_scale0, error)
        && ReadOptionalFloat(backend, "inv_mass_scale1",
          out.backend_scalars.physx.inv_mass_scale1, error)
        && ReadOptionalFloat(backend, "inv_inertia_scale0",
          out.backend_scalars.physx.inv_inertia_scale0, error)
        && ReadOptionalFloat(backend, "inv_inertia_scale1",
          out.backend_scalars.physx.inv_inertia_scale1, error);
    }
    error = "Field 'backend.target' has unsupported value";
    return false;
  }

  auto ParseVehicleWheelBackend(const nlohmann::json& obj,
    phys::VehicleWheelBackendScalars& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    const auto target = backend.at("target").get<std::string>();
    if (target == "jolt") {
      return ReadOptionalFloat(
        backend, "wheel_castor", out.jolt.wheel_castor, error);
    }
    if (target == "physx") {
      return true;
    }
    error = "Field 'backend.target' has unsupported value";
    return false;
  }

  auto ParseRigidBodyBinding(const nlohmann::json& binding,
    RigidBodyBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "material_ref", out.material_ref, error)) {
      return false;
    }

    out.record.node_index = node_index;
    if (!ParseBodyType(binding, out.record.body_type, error)
      || !ParseMotionQuality(binding, out.record.motion_quality, error)
      || !ReadOptionalUInt16(
        binding, "collision_layer", out.record.collision_layer, error)
      || !ReadOptionalUInt32(
        binding, "collision_mask", out.record.collision_mask, error)
      || !ReadOptionalFloat(binding, "mass", out.record.mass, error)
      || !ReadOptionalFloat(
        binding, "linear_damping", out.record.linear_damping, error)
      || !ReadOptionalFloat(
        binding, "angular_damping", out.record.angular_damping, error)
      || !ReadOptionalFloat(
        binding, "gravity_factor", out.record.gravity_factor, error)
      || !ReadOptionalFloat3(binding, "center_of_mass_override",
        out.record.com_override, out.record.has_com_override, error)
      || !ReadOptionalFloat3(binding, "inertia_tensor_override",
        out.record.inertia_override, out.record.has_inertia_override, error)
      || !ReadOptionalFloat(
        binding, "max_linear_velocity", out.record.max_linear_velocity, error)
      || !ReadOptionalFloat(
        binding, "max_angular_velocity", out.record.max_angular_velocity, error)
      || !ParseAllowedDofFlags(binding, out.record.allowed_dof_flags, error)
      || !ParseRigidBodyBackend(binding, out.record, error)) {
      return false;
    }

    auto initial_activation = true;
    auto is_sensor = false;
    if (!ReadOptionalBool(
          binding, "initial_activation", initial_activation, error)
      || !ReadOptionalBool(binding, "is_sensor", is_sensor, error)) {
      return false;
    }
    out.record.initial_activation = initial_activation ? 1U : 0U;
    out.record.is_sensor = is_sensor ? 1U : 0U;
    return true;
  }

  auto ParseColliderBinding(const nlohmann::json& binding,
    ColliderBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "material_ref", out.material_ref, error)) {
      return false;
    }

    out.record.node_index = node_index;
    return ReadOptionalUInt16(
             binding, "collision_layer", out.record.collision_layer, error)
      && ReadOptionalUInt32(
        binding, "collision_mask", out.record.collision_mask, error);
  }

  auto ParseCharacterBinding(const nlohmann::json& binding,
    CharacterBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref, error)) {
      return false;
    }

    out.record.node_index = node_index;
    return ReadOptionalFloat(binding, "mass", out.record.mass, error)
      && ReadOptionalFloat(
        binding, "max_slope_angle", out.record.max_slope_angle, error)
      && ReadOptionalFloat(
        binding, "step_height", out.record.step_height, error)
      && ReadOptionalFloat(
        binding, "step_down_distance", out.record.step_down_distance, error)
      && ReadOptionalFloat(
        binding, "max_strength", out.record.max_strength, error)
      && ReadOptionalFloat(binding, "skin_width", out.record.skin_width, error)
      && ReadOptionalFloat(binding, "predictive_contact_distance",
        out.record.predictive_contact_distance, error)
      && ReadOptionalUInt16(
        binding, "collision_layer", out.record.collision_layer, error)
      && ReadOptionalUInt32(
        binding, "collision_mask", out.record.collision_mask, error)
      && ReadOptionalString(
        binding, "inner_shape_ref", out.inner_shape_ref, error)
      && ParseCharacterBackend(binding, out.record, error);
  }

  auto ParseSoftBodyBinding(const nlohmann::json& binding,
    SoftBodyBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(
          binding, "source_mesh_ref", out.source_mesh_ref, error)) {
      return false;
    }
    out.record.node_index = node_index;
    auto self_collision = false;
    if (!ReadOptionalBool(binding, "self_collision", self_collision, error)) {
      return false;
    }
    out.record.self_collision = self_collision ? 1U : 0U;

    return ReadOptionalString(
             binding, "collision_mesh_ref", out.collision_mesh_ref, error)
      && ReadRequiredFloat(
        binding, "edge_compliance", out.record.edge_compliance, error)
      && ReadRequiredFloat(
        binding, "shear_compliance", out.record.shear_compliance, error)
      && ReadRequiredFloat(
        binding, "bend_compliance", out.record.bend_compliance, error)
      && ReadRequiredFloat(
        binding, "volume_compliance", out.record.volume_compliance, error)
      && ReadRequiredFloat(
        binding, "pressure_coefficient", out.record.pressure_coefficient, error)
      && ParseTetherMode(binding, out.record.tether_mode, error)
      && ReadRequiredFloat(binding, "tether_max_distance_multiplier",
        out.record.tether_max_distance_multiplier, error)
      && ReadRequiredFloat(
        binding, "global_damping", out.record.global_damping, error)
      && ReadRequiredFloat(
        binding, "restitution", out.record.restitution, error)
      && ReadRequiredFloat(binding, "friction", out.record.friction, error)
      && ReadRequiredUInt32(binding, "solver_iteration_count",
        out.record.solver_iteration_count, error)
      && ReadRequiredFloat(
        binding, "vertex_radius", out.record.vertex_radius, error)
      && ReadRequiredUInt32Array(
        binding, "pinned_vertices", out.pinned_vertices, error)
      && ReadRequiredUInt32Array(
        binding, "kinematic_vertices", out.kinematic_vertices, error)
      && ParseSoftBodyBackend(binding, out.record, error);
  }

  auto ParseJointBinding(const nlohmann::json& binding, JointBindingSource& out,
    std::string& error) -> bool
  {
    uint32_t node_index_a = 0;
    uint32_t node_index_b = 0;
    if (!ReadRequiredUInt32(binding, "node_index_a", node_index_a, error)
      || !ReadJointNodeIndexB(binding, node_index_b, error)
      || !ReadOptionalString(
        binding, "constraint_ref", out.constraint_ref, error)) {
      return false;
    }
    out.record.node_index_a = node_index_a;
    out.record.node_index_b = node_index_b;
    return ParseJointBackend(binding, out.record, error);
  }

  auto ParseVehicleBinding(const nlohmann::json& binding,
    VehicleBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)
      || !ReadOptionalString(
        binding, "constraint_ref", out.constraint_ref, error)) {
      return false;
    }
    out.record.node_index = node_index;

    const auto& wheels = binding.at("wheels");
    out.wheels.clear();
    out.wheels.reserve(wheels.size());
    for (size_t i = 0; i < wheels.size(); ++i) {
      const auto& wheel = wheels[i];
      auto wheel_source = VehicleWheelSource {};
      uint32_t wheel_node_index = 0;
      uint32_t axle_index_u32 = 0;
      if (!ReadRequiredUInt32(wheel, "node_index", wheel_node_index, error)
        || !ReadRequiredUInt32(wheel, "axle_index", axle_index_u32, error)
        || axle_index_u32 > (std::numeric_limits<uint16_t>::max)()
        || !ParseWheelSide(wheel, wheel_source.side, error)
        || !ParseVehicleWheelBackend(
          wheel, wheel_source.backend_scalars, error)) {
        if (axle_index_u32 > (std::numeric_limits<uint16_t>::max)()) {
          error = "Field 'axle_index' exceeds uint16 range";
        }
        return false;
      }
      wheel_source.node_index = wheel_node_index;
      wheel_source.axle_index = static_cast<uint16_t>(axle_index_u32);
      out.wheels.push_back(wheel_source);
    }
    return true;
  }

  auto ParseAggregateBinding(const nlohmann::json& binding,
    phys::AggregateBindingRecord& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    out.node_index = node_index;
    if (!ReadOptionalUInt32(binding, "max_bodies", out.max_bodies, error)
      || !ParseAggregateAuthority(binding, out.authority, error)) {
      return false;
    }
    if (binding.contains("filter_overlap")) {
      out.filter_overlap = binding.at("filter_overlap").get<bool>() ? 1U : 0U;
    }
    return true;
  }

  template <typename RecordT, typename ParseFn>
  auto ParseBindingsArray(const nlohmann::json& bindings, const char* key,
    std::vector<RecordT>& out, ImportSession& session,
    const ImportRequest& request, ParseFn&& parse_fn) -> void
  {
    if (!bindings.contains(key)) {
      return;
    }

    const auto& array = bindings.at(key);
    out.reserve(array.size());
    for (size_t i = 0; i < array.size(); ++i) {
      const auto object_path
        = std::string("bindings.") + key + "[" + std::to_string(i) + "]";
      auto record = RecordT {};
      auto error = std::string {};
      if (!parse_fn(array[i], record, error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.schema_contract_mismatch", std::move(error),
          object_path);
        continue;
      }
      out.push_back(std::move(record));
    }
  }

  auto ValidateSingletonPerNode(const std::span<const uint32_t> node_indices,
    std::string_view category, ImportSession& session,
    const ImportRequest& request) -> void
  {
    auto seen = std::unordered_map<uint32_t, size_t> {};
    for (size_t i = 0; i < node_indices.size(); ++i) {
      const auto [it, inserted] = seen.emplace(node_indices[i], i);
      if (!inserted) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Duplicate singleton binding for node "
            + std::to_string(node_indices[i]) + " in " + std::string(category),
          std::string("bindings.") + std::string(category) + "["
            + std::to_string(i) + "]");
      }
    }
  }

  template <typename RecordT, typename NodeIndexFn>
  auto ValidateSingletonBindings(const std::vector<RecordT>& records,
    std::string_view category, NodeIndexFn&& node_index_of,
    ImportSession& session, const ImportRequest& request) -> void
  {
    auto singleton_nodes = std::vector<uint32_t> {};
    singleton_nodes.reserve(records.size());
    for (const auto& record : records) {
      singleton_nodes.push_back(node_index_of(record));
    }
    ValidateSingletonPerNode(singleton_nodes, category, session, request);
  }

  auto ParseSidecarDocument(std::span<const std::byte> bytes,
    ImportSession& session, const ImportRequest& request)
    -> std::optional<PhysicsSidecarDocument>
  {
    std::string source_text;
    source_text.resize(bytes.size());
    if (!bytes.empty()) {
      std::memcpy(source_text.data(), bytes.data(), bytes.size());
    }

    auto doc = nlohmann::json {};
    try {
      doc = nlohmann::json::parse(source_text);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.payload_parse_failed",
        "Failed to parse sidecar document as JSON: " + std::string(ex.what()));
      return std::nullopt;
    }

    if (!ValidatePhysicsSchema(doc, session, request)) {
      return std::nullopt;
    }

    auto parsed = PhysicsSidecarDocument {};
    try {
      const auto& bindings = doc.at("bindings");
      ParseBindingsArray(bindings, "rigid_bodies", parsed.rigid_bodies, session,
        request, ParseRigidBodyBinding);
      ParseBindingsArray(bindings, "colliders", parsed.colliders, session,
        request, ParseColliderBinding);
      ParseBindingsArray(bindings, "characters", parsed.characters, session,
        request, ParseCharacterBinding);
      ParseBindingsArray(bindings, "soft_bodies", parsed.soft_bodies, session,
        request, ParseSoftBodyBinding);
      ParseBindingsArray(
        bindings, "joints", parsed.joints, session, request, ParseJointBinding);
      ParseBindingsArray(bindings, "vehicles", parsed.vehicles, session,
        request, ParseVehicleBinding);
      ParseBindingsArray(bindings, "aggregates", parsed.aggregates, session,
        request, ParseAggregateBinding);
      ValidateSingletonBindings(
        parsed.rigid_bodies, "rigid_bodies",
        [](const auto& record) { return record.record.node_index; }, session,
        request);
      ValidateSingletonBindings(
        parsed.characters, "characters",
        [](const auto& record) { return record.record.node_index; }, session,
        request);
      ValidateSingletonBindings(
        parsed.soft_bodies, "soft_bodies",
        [](const auto& record) { return record.record.node_index; }, session,
        request);
      ValidateSingletonBindings(
        parsed.vehicles, "vehicles",
        [](const auto& record) { return record.record.node_index; }, session,
        request);
      ValidateSingletonBindings(
        parsed.aggregates, "aggregates",
        [](const auto& record) { return record.node_index; }, session, request);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.schema_contract_mismatch",
        "Schema validated sidecar document failed invariant extraction: "
          + std::string(ex.what()));
      return std::nullopt;
    }

    if (session.HasErrors()) {
      return std::nullopt;
    }
    return parsed;
  }

  using AssetTypeMap = std::unordered_map<data::AssetKey, data::AssetType>;

  auto BuildAssetTypeMap(const lc::Inspection& inspection) -> AssetTypeMap
  {
    auto out = AssetTypeMap {};
    const auto assets = inspection.Assets();
    out.reserve(assets.size());
    for (const auto& asset : assets) {
      out.insert_or_assign(
        asset.key, static_cast<data::AssetType>(asset.asset_type));
    }
    return out;
  }

  struct BindingValidationContext final {
    ImportSession& session;
    const ImportRequest& request;
    content::VirtualPathResolver& resolver;
    std::span<const SidecarCookedInspectionContext> cooked_contexts;
    const AssetTypeMap& target_asset_types;
    const std::filesystem::path& target_cooked_root;
    uint32_t node_count = 0;
  };

  auto ResolvePhysicsResourceRef(BindingValidationContext& ctx,
    std::string_view reference_path, std::string_view object_path,
    phys::PhysicsResourceFormat expected_format,
    std::string_view expected_format_name, std::string_view diagnostic_prefix)
    -> std::optional<data::pak::core::ResourceIndexT>;

  auto ResolveAssetKeyForVirtualPath(BindingValidationContext& ctx,
    std::string_view virtual_path, std::string_view object_path,
    const data::AssetType expected_type, std::string_view unresolved_code,
    std::string_view wrong_type_code) -> std::optional<data::AssetKey>
  {
    auto resolved_key = std::optional<data::AssetKey> {};
    try {
      resolved_key = ctx.resolver.ResolveAssetKey(virtual_path);
    } catch (const std::exception& ex) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string(unresolved_code),
        "Invalid virtual path: " + std::string(ex.what()),
        std::string(object_path));
      return std::nullopt;
    }

    if (!resolved_key.has_value()) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string(unresolved_code),
        "Resolved key not found for virtual path: " + std::string(virtual_path),
        std::string(object_path));
      return std::nullopt;
    }

    const auto target_it = ctx.target_asset_types.find(*resolved_key);
    if (target_it == ctx.target_asset_types.end()) {
      bool found_in_other_source = false;
      for (const auto& context : ctx.cooked_contexts) {
        for (const auto& asset : context.inspection.Assets()) {
          if (asset.key == *resolved_key) {
            found_in_other_source = true;
            break;
          }
        }
        if (found_in_other_source) {
          break;
        }
      }

      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        found_in_other_source ? "physics.sidecar.reference_source_mismatch"
                              : std::string(unresolved_code),
        found_in_other_source
          ? "Resolved reference is not in the target scene source domain"
          : "Resolved key not found for virtual path: "
            + std::string(virtual_path),
        std::string(object_path));
      return std::nullopt;
    }

    if (target_it->second != expected_type) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string(wrong_type_code),
        "Resolved reference has unexpected asset type",
        std::string(object_path));
      return std::nullopt;
    }

    return *resolved_key;
  }

  auto ValidateNodeIndex(const uint32_t node_index, const uint32_t node_count,
    ImportSession& session, const ImportRequest& request,
    std::string_view object_path) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnosticAtPath(session, request, ImportSeverity::kError,
      "physics.sidecar.node_ref_out_of_bounds",
      "Node index is out of bounds for target scene", std::string(object_path));
    return false;
  }

  template <typename BindingSourceT>
  auto ResolveShapeAndMaterialBindings(std::vector<BindingSourceT>& bindings,
    std::string_view category, BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < bindings.size(); ++i) {
      auto& binding = bindings[i];
      const auto base_path = std::string("bindings.") + std::string(category)
        + "[" + std::to_string(i) + "]";
      if (!ValidateNodeIndex(binding.record.node_index, ctx.node_count,
            ctx.session, ctx.request, base_path)) {
        continue;
      }

      const auto shape_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.shape_ref,
          base_path + ".shape_ref", data::AssetType::kCollisionShape,
          "physics.sidecar.shape_ref_unresolved",
          "physics.sidecar.shape_ref_not_collision_shape");
      const auto material_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.material_ref,
          base_path + ".material_ref", data::AssetType::kPhysicsMaterial,
          "physics.sidecar.material_ref_unresolved",
          "physics.sidecar.material_ref_not_physics_material");
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
      auto& binding = bindings[i];
      const auto base_path = std::string("bindings.") + std::string(category)
        + "[" + std::to_string(i) + "]";
      if (!ValidateNodeIndex(binding.record.node_index, ctx.node_count,
            ctx.session, ctx.request, base_path)) {
        continue;
      }

      const auto shape_key
        = ResolveAssetKeyForVirtualPath(ctx, binding.shape_ref,
          base_path + ".shape_ref", data::AssetType::kCollisionShape,
          "physics.sidecar.shape_ref_unresolved",
          "physics.sidecar.shape_ref_not_collision_shape");
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
      auto& binding = bindings[i];
      if (!binding.inner_shape_ref.has_value()) {
        continue;
      }
      const auto base_path
        = std::string("bindings.characters[") + std::to_string(i) + "]";
      const auto inner_shape_key
        = ResolveAssetKeyForVirtualPath(ctx, *binding.inner_shape_ref,
          base_path + ".inner_shape_ref", data::AssetType::kCollisionShape,
          "physics.sidecar.inner_shape_ref_unresolved",
          "physics.sidecar.inner_shape_ref_not_collision_shape");
      if (inner_shape_key.has_value()) {
        binding.record.inner_shape_asset_key = *inner_shape_key;
      }
    }
  }

  template <typename RecordT, typename NodeIndexFn>
  auto ValidateNodeBindings(const std::vector<RecordT>& records,
    std::string_view category, NodeIndexFn&& node_index_of,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto path = std::string("bindings.") + std::string(category) + "["
        + std::to_string(i) + "]";
      (void)ValidateNodeIndex(node_index_of(records[i]), ctx.node_count,
        ctx.session, ctx.request, path);
    }
  }

  auto ResolveSoftBodyBindings(std::vector<SoftBodyBindingSource>& records,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.soft_bodies[") + std::to_string(i) + "]";
      if (!ValidateNodeIndex(records[i].record.node_index, ctx.node_count,
            ctx.session, ctx.request, base_path + ".node_index")) {
        continue;
      }

      const auto is_finite_non_negative = [](const float value) {
        return std::isfinite(value) && value >= 0.0F;
      };
      if (!is_finite_non_negative(records[i].record.edge_compliance)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.edge_compliance must be finite and >= 0",
          base_path + ".edge_compliance");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.shear_compliance)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.shear_compliance must be finite and >= 0",
          base_path + ".shear_compliance");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.bend_compliance)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.bend_compliance must be finite and >= 0",
          base_path + ".bend_compliance");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.volume_compliance)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.volume_compliance must be finite and >= 0",
          base_path + ".volume_compliance");
        continue;
      }
      if (!std::isfinite(records[i].record.pressure_coefficient)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.pressure_coefficient must be finite",
          base_path + ".pressure_coefficient");
        continue;
      }
      if (!is_finite_non_negative(
            records[i].record.tether_max_distance_multiplier)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.tether_max_distance_multiplier must be finite and >= 0",
          base_path + ".tether_max_distance_multiplier");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.global_damping)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.global_damping must be finite and >= 0",
          base_path + ".global_damping");
        continue;
      }

      if (!is_finite_non_negative(records[i].record.restitution)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.restitution must be finite and >= 0",
          base_path + ".restitution");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.friction)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.friction must be finite and >= 0",
          base_path + ".friction");
        continue;
      }
      if (!is_finite_non_negative(records[i].record.vertex_radius)) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.vertex_radius must be finite and >= 0",
          base_path + ".vertex_radius");
        continue;
      }
      if (records[i].record.solver_iteration_count == 0U) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.solver_iteration_count must be greater than zero",
          base_path + ".solver_iteration_count");
        continue;
      }
      if (records[i].pinned_vertices.size()
        > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.pinned_vertices exceeds uint32 range",
          base_path + ".pinned_vertices");
        continue;
      }
      if (records[i].kinematic_vertices.size()
        > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "soft_bodies.kinematic_vertices exceeds uint32 range",
          base_path + ".kinematic_vertices");
        continue;
      }

      records[i].record.pinned_vertex_count
        = static_cast<uint32_t>(records[i].pinned_vertices.size());
      records[i].record.kinematic_vertex_count
        = static_cast<uint32_t>(records[i].kinematic_vertices.size());

      (void)ResolveAssetKeyForVirtualPath(ctx, records[i].source_mesh_ref,
        base_path + ".source_mesh_ref", data::AssetType::kGeometry,
        "physics.sidecar.source_mesh_ref_unresolved",
        "physics.sidecar.source_mesh_ref_not_geometry");

      if (records[i].collision_mesh_ref.has_value()) {
        (void)ResolveAssetKeyForVirtualPath(ctx, *records[i].collision_mesh_ref,
          base_path + ".collision_mesh_ref", data::AssetType::kGeometry,
          "physics.sidecar.collision_mesh_ref_unresolved",
          "physics.sidecar.collision_mesh_ref_not_geometry");
      }
    }
  }

  [[nodiscard]] auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    auto in = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
      return std::nullopt;
    }

    const auto end = in.tellg();
    if (end < 0) {
      return std::nullopt;
    }

    const auto size = static_cast<size_t>(end);
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    if (size > 0U) {
      in.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size));
      if (!in.good()) {
        return std::nullopt;
      }
    }
    return bytes;
  }

  auto ResolvePhysicsResourceRef(BindingValidationContext& ctx,
    std::string_view reference_path, std::string_view object_path,
    const phys::PhysicsResourceFormat expected_format,
    std::string_view expected_format_name, std::string_view diagnostic_prefix)
    -> std::optional<data::pak::core::ResourceIndexT>
  {
    if (!internal::IsCanonicalVirtualPath(reference_path)) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_invalid",
        std::string(diagnostic_prefix) + " must be canonical",
        std::string(object_path));
      return std::nullopt;
    }

    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          ctx.request, reference_path, relpath)) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_unmounted",
        std::string(diagnostic_prefix) + " is outside mounted cooked roots",
        std::string(object_path));
      return std::nullopt;
    }
    relpath = NormalizeRelPath(std::move(relpath));

    const auto target_file
      = ctx.target_cooked_root / std::filesystem::path(relpath);
    if (!std::filesystem::exists(target_file)) {
      auto found_elsewhere = false;
      for (const auto& mounted : ctx.cooked_contexts) {
        if (mounted.cooked_root == ctx.target_cooked_root) {
          continue;
        }
        const auto candidate
          = mounted.cooked_root / std::filesystem::path(relpath);
        if (std::filesystem::exists(candidate)) {
          found_elsewhere = true;
          break;
        }
      }

      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        found_elsewhere ? "physics.sidecar.reference_source_mismatch"
                        : std::string("physics.sidecar.")
            + std::string(diagnostic_prefix) + "_unresolved",
        found_elsewhere
          ? std::string("Resolved ") + std::string(diagnostic_prefix)
            + " is not in the target source domain"
          : std::string("Resolved ") + std::string(diagnostic_prefix)
            + " was not found: " + std::string(reference_path),
        std::string(object_path));
      return std::nullopt;
    }

    const auto sidecar_bytes = ReadBinaryFile(target_file);
    if (!sidecar_bytes.has_value()) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_read_failed",
        std::string("Failed reading ") + std::string(diagnostic_prefix)
          + " sidecar: " + target_file.string(),
        std::string(object_path));
      return std::nullopt;
    }

    auto parsed = internal::ParsedPhysicsResourceDescriptorSidecar {};
    auto parse_error = std::string {};
    if (!internal::ParsePhysicsResourceDescriptorSidecar(
          *sidecar_bytes, parsed, parse_error)) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_parse_failed",
        std::string("Failed parsing ") + std::string(diagnostic_prefix)
          + " sidecar: " + parse_error,
        std::string(object_path));
      return std::nullopt;
    }

    if (parsed.descriptor.format != expected_format) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_format_mismatch",
        std::string(diagnostic_prefix)
          + " must reference a physics resource with format '"
          + std::string(expected_format_name) + "'",
        std::string(object_path));
      return std::nullopt;
    }

    if (parsed.resource_index == data::pak::core::kNoResourceIndex) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string("physics.sidecar.") + std::string(diagnostic_prefix)
          + "_unresolved",
        std::string(diagnostic_prefix)
          + " sidecar references invalid resource index zero",
        std::string(object_path));
      return std::nullopt;
    }

    return parsed.resource_index;
  }

  auto ResolveJointBindings(std::vector<JointBindingSource>& records,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.joints[") + std::to_string(i) + "]";
      const bool node_a_ok = ValidateNodeIndex(records[i].record.node_index_a,
        ctx.node_count, ctx.session, ctx.request, base_path + ".node_index_a");
      const bool node_b_ok
        = (records[i].record.node_index_b == phys::kWorldAttachmentNodeIndex)
        || ValidateNodeIndex(records[i].record.node_index_b, ctx.node_count,
          ctx.session, ctx.request, base_path + ".node_index_b");
      if (!(node_a_ok && node_b_ok)) {
        continue;
      }

      if (records[i].constraint_ref.has_value()) {
        const auto constraint_index = ResolvePhysicsResourceRef(ctx,
          *records[i].constraint_ref, base_path + ".constraint_ref",
          phys::PhysicsResourceFormat::kJoltConstraintBinary,
          "jolt_constraint_binary", "constraint_ref");
        if (constraint_index.has_value()) {
          records[i].record.constraint_resource_index = *constraint_index;
        }
      }
    }
  }

  auto ResolveVehicleBindings(std::vector<VehicleBindingSource>& records,
    std::vector<phys::VehicleWheelBindingRecord>& wheel_records,
    BindingValidationContext& ctx) -> void
  {
    wheel_records.clear();
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.vehicles[") + std::to_string(i) + "]";
      if (!ValidateNodeIndex(records[i].record.node_index, ctx.node_count,
            ctx.session, ctx.request, base_path + ".node_index")) {
        continue;
      }

      if (records[i].constraint_ref.has_value()) {
        const auto constraint_index = ResolvePhysicsResourceRef(ctx,
          *records[i].constraint_ref, base_path + ".constraint_ref",
          phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary,
          "jolt_vehicle_constraint_binary", "constraint_ref");
        if (constraint_index.has_value()) {
          records[i].record.constraint_resource_index = *constraint_index;
        }
      }

      if (records[i].wheels.size() < 2U) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Vehicle must declare at least two wheels", base_path + ".wheels");
        continue;
      }

      auto wheel_nodes = std::unordered_set<uint32_t> {};
      auto wheel_roles
        = std::unordered_set<uint32_t> {}; // packed (axle << 16) | side
      const auto wheel_offset = wheel_records.size();
      auto slice_count = uint16_t { 0 };
      for (size_t w = 0; w < records[i].wheels.size(); ++w) {
        const auto wheel_path
          = base_path + ".wheels[" + std::to_string(w) + "]";
        const auto& wheel = records[i].wheels[w];
        if (!ValidateNodeIndex(wheel.node_index, ctx.node_count, ctx.session,
              ctx.request, wheel_path + ".node_index")) {
          continue;
        }
        if (wheel.node_index == records[i].record.node_index) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "Vehicle wheel node_index must differ from chassis node_index",
            wheel_path + ".node_index");
          continue;
        }
        if (!wheel_nodes.insert(wheel.node_index).second) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "Vehicle wheels must reference distinct node_index values",
            wheel_path + ".node_index");
          continue;
        }
        const auto role_key = (static_cast<uint32_t>(wheel.axle_index) << 16U)
          | static_cast<uint32_t>(wheel.side);
        if (!wheel_roles.insert(role_key).second) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "Vehicle wheel role (axle_index + side) must be unique",
            wheel_path);
          continue;
        }
        if (slice_count == (std::numeric_limits<uint16_t>::max)()) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "Vehicle wheel count exceeds uint16 range", wheel_path);
          continue;
        }

        wheel_records.push_back(phys::VehicleWheelBindingRecord {
          .vehicle_node_index = records[i].record.node_index,
          .wheel_node_index = wheel.node_index,
          .axle_index = wheel.axle_index,
          .side = wheel.side,
          .backend_scalars = wheel.backend_scalars,
        });
        slice_count = static_cast<uint16_t>(slice_count + 1U);
      }

      if (slice_count < 2U) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Vehicle wheel bindings must resolve to at least two valid wheels",
          base_path + ".wheels");
        wheel_records.resize(wheel_offset);
        continue;
      }

      if (wheel_offset > (std::numeric_limits<uint32_t>::max)()) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.payload_invalid",
          "Vehicle wheel table offset exceeds uint32 range", base_path);
        wheel_records.resize(wheel_offset);
        continue;
      }

      records[i].record.wheel_slice_offset
        = static_cast<uint32_t>(wheel_offset);
      records[i].record.wheel_slice_count = slice_count;
    }
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
      } else if (trailing_cursor > (std::numeric_limits<uint32_t>::max)()) {
        return std::nullopt;
      } else {
        record.pinned_vertex_byte_offset
          = static_cast<uint32_t>(trailing_cursor);
        trailing_cursor += pinned_size_bytes;
      }

      if (source.kinematic_vertices.empty()) {
        record.kinematic_vertex_byte_offset = 0U;
      } else if (trailing_cursor > (std::numeric_limits<uint32_t>::max)()) {
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

      std::memcpy(blob.bytes.data() + previous_size, &record, fixed_size);
      auto write_cursor = previous_size + fixed_size;
      if (pinned_size_bytes > 0U) {
        std::memcpy(blob.bytes.data() + write_cursor,
          source.pinned_vertices.data(),
          static_cast<size_t>(pinned_size_bytes));
        write_cursor += static_cast<size_t>(pinned_size_bytes);
      }
      if (kinematic_size_bytes > 0U) {
        std::memcpy(blob.bytes.data() + write_cursor,
          source.kinematic_vertices.data(),
          static_cast<size_t>(kinematic_size_bytes));
      }
    }

    return blob;
  }

  auto SerializePhysicsSceneAsset(const SidecarResolvedSceneState& scene_state,
    std::string_view sidecar_name, const std::vector<TableBlob>& input_tables,
    const bool hashing_enabled, std::string& error)
    -> std::optional<std::vector<std::byte>>
  {
    auto tables = input_tables;
    std::ranges::sort(tables, [](const TableBlob& lhs, const TableBlob& rhs) {
      return static_cast<uint32_t>(lhs.binding_type)
        < static_cast<uint32_t>(rhs.binding_type);
    });

    auto desc = phys::PhysicsSceneAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
    desc.header.version = phys::kPhysicsSceneAssetVersion;
    util::TruncateAndNullTerminate(
      desc.header.name, sizeof(desc.header.name), sidecar_name);
    desc.target_scene_key = scene_state.scene_key;
    desc.target_node_count = scene_state.node_count;
    desc.component_table_count = static_cast<uint32_t>(tables.size());
    desc.component_table_directory_offset = tables.empty()
      ? 0U
      : static_cast<data::pak::core::OffsetT>(sizeof(desc));

    auto directory_entries = std::vector<phys::PhysicsComponentTableDesc> {};
    directory_entries.reserve(tables.size());

    uint64_t cursor = tables.empty() ? 0U
                                     : static_cast<uint64_t>(sizeof(desc))
        + static_cast<uint64_t>(tables.size())
          * sizeof(phys::PhysicsComponentTableDesc);

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
      if (cursor > (std::numeric_limits<data::pak::core::OffsetT>::max)()) {
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
    if (hashing_enabled) {
      PatchContentHash(bytes, util::ComputeContentHash(bytes));
    }
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

  auto LoadCookedContextsAndMountResolver(ImportSession& session,
    const ImportRequest& request,
    std::vector<SidecarCookedInspectionContext>& cooked_contexts,
    content::VirtualPathResolver& resolver) -> bool
  {
    cooked_contexts.clear();
    cooked_contexts.reserve(1U + request.cooked_context_roots.size());

    auto primary_context = SidecarCookedInspectionContext {};
    if (!detail::LoadCookedInspectionContext(session.CookedRoot(), session,
          request, kPhysicsSidecarResolverDiagnostics, primary_context)) {
      return false;
    }
    cooked_contexts.push_back(std::move(primary_context));

    for (const auto& context_root : request.cooked_context_roots) {
      auto context = SidecarCookedInspectionContext {};
      if (!detail::LoadCookedInspectionContext(context_root, session, request,
            kPhysicsSidecarResolverDiagnostics, context)) {
        return false;
      }
      cooked_contexts.push_back(std::move(context));
    }

    for (const auto& context : cooked_contexts) {
      try {
        resolver.AddLooseCookedRoot(context.cooked_root);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.sidecar.resolver_mount_failed",
          "Failed mounting cooked root for sidecar resolution: "
            + context.cooked_root.string() + " (" + ex.what() + ")");
        return false;
      }
    }

    return true;
  }

  auto ResolveAndValidateBindings(PhysicsSidecarDocument& parsed,
    const SidecarResolvedSceneState& resolved_scene_state,
    std::span<const SidecarCookedInspectionContext> cooked_contexts,
    content::VirtualPathResolver& resolver, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    const auto* target_context = detail::ResolveSceneInspectionContextByKey(
      cooked_contexts, resolved_scene_state.scene_key);
    if (target_context == nullptr) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.target_scene_missing",
        "Resolved target scene key is not present in cooked scene context");
      return false;
    }

    const auto target_asset_types
      = BuildAssetTypeMap(target_context->inspection);
    auto validation_ctx = BindingValidationContext { session, request, resolver,
      cooked_contexts, target_asset_types, target_context->cooked_root,
      resolved_scene_state.node_count };

    ResolveShapeAndMaterialBindings(
      parsed.rigid_bodies, "rigid_bodies", validation_ctx);
    ResolveShapeAndMaterialBindings(
      parsed.colliders, "colliders", validation_ctx);
    ResolveCharacterBindings(parsed.characters, validation_ctx);
    ResolveSoftBodyBindings(parsed.soft_bodies, validation_ctx);
    ResolveJointBindings(parsed.joints, validation_ctx);
    ResolveVehicleBindings(
      parsed.vehicles, parsed.vehicle_wheels, validation_ctx);
    ValidateNodeBindings(
      parsed.aggregates, "aggregates",
      [](const auto& record) { return record.node_index; }, validation_ctx);

    return !session.HasErrors();
  }

  auto BuildPhysicsSidecarTables(const PhysicsSidecarDocument& parsed)
    -> std::vector<TableBlob>
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
    auto vehicle_records
      = ExtractRecordVector(parsed.vehicles, &VehicleBindingSource::record);
    auto wheel_records = parsed.vehicle_wheels;
    auto aggregate_records = parsed.aggregates;

    auto tables = std::vector<TableBlob> {};
    SortAndAppendTable(tables, phys::PhysicsBindingType::kRigidBody,
      rigid_records, [](const auto& lhs, const auto& rhs) {
        return lhs.node_index < rhs.node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kCollider,
      collider_records, [](const auto& lhs, const auto& rhs) {
        return lhs.node_index < rhs.node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kCharacter,
      character_records, [](const auto& lhs, const auto& rhs) {
        return lhs.node_index < rhs.node_index;
      });
    std::ranges::sort(soft_body_records, [](const auto& lhs, const auto& rhs) {
      return lhs.record.node_index < rhs.record.node_index;
    });
    if (const auto soft_body_table = MakeSoftBodyTableBlob(soft_body_records);
      soft_body_table.has_value()) {
      tables.push_back(*soft_body_table);
    }
    SortAndAppendTable(tables, phys::PhysicsBindingType::kJoint, joint_records,
      [](const auto& lhs, const auto& rhs) {
        if (lhs.node_index_a != rhs.node_index_a) {
          return lhs.node_index_a < rhs.node_index_a;
        }
        return lhs.node_index_b < rhs.node_index_b;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kVehicle,
      vehicle_records, [](const auto& lhs, const auto& rhs) {
        return lhs.node_index < rhs.node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kVehicleWheel,
      wheel_records, [](const auto& lhs, const auto& rhs) {
        if (lhs.vehicle_node_index != rhs.vehicle_node_index) {
          return lhs.vehicle_node_index < rhs.vehicle_node_index;
        }
        if (lhs.axle_index != rhs.axle_index) {
          return lhs.axle_index < rhs.axle_index;
        }
        if (lhs.side != rhs.side) {
          return static_cast<uint32_t>(lhs.side)
            < static_cast<uint32_t>(rhs.side);
        }
        return lhs.wheel_node_index < rhs.wheel_node_index;
      });
    SortAndAppendTable(tables, phys::PhysicsBindingType::kAggregate,
      aggregate_records, [](const auto& lhs, const auto& rhs) {
        return lhs.node_index < rhs.node_index;
      });

    return tables;
  }

  auto SerializeAndEmitPhysicsSidecar(
    const SidecarResolvedSceneState& scene_state, const ImportRequest& request,
    const std::vector<TableBlob>& tables, ImportSession& session) -> bool
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
        EffectiveContentHashingEnabled(request.options.with_content_hashing),
        serialize_error);
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
        std::span<const std::byte>(*descriptor_bytes));
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.descriptor_emit_failed",
        "Failed to emit physics sidecar descriptor: " + std::string(ex.what()));
      return false;
    }

    return true;
  }

} // namespace

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
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
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
  if (!ValidatePhysicsSidecarRequest(*session, request)) {
    co_return false;
  }

  auto parsed = ParseSidecarDocument(item.source_bytes, *session, request);
  if (!parsed.has_value()) {
    co_return false;
  }

  auto* const reader = session->FileReader().get();
  if (reader == nullptr) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.sidecar.io_unavailable",
      "Physics sidecar import requires an async file reader");
    co_return false;
  }

  auto cooked_contexts = std::vector<SidecarCookedInspectionContext> {};
  auto resolver = content::VirtualPathResolver {};
  if (!LoadCookedContextsAndMountResolver(
        *session, request, cooked_contexts, resolver)) {
    co_return false;
  }

  auto resolved_scene_state
    = co_await detail::ResolveTargetSceneState(*session, request, resolver,
      cooked_contexts, *reader, request.physics->target_scene_virtual_path,
      kPhysicsSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  if (!ResolveAndValidateBindings(*parsed, *resolved_scene_state,
        cooked_contexts, resolver, *session, request)) {
    co_return false;
  }

  const auto tables = BuildPhysicsSidecarTables(*parsed);
  if (!SerializeAndEmitPhysicsSidecar(
        *resolved_scene_state, request, tables, *session)) {
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
