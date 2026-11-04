//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::import {
namespace {

  namespace phys = data::pak::physics;
  using core::meta::physics::PhysicsBackend;

  //! Collects the diagnostics of one document.
  struct Reporter final {
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics;
    std::string source_path;

    auto Error(std::string code, std::string message) const -> void
    {
      this->diagnostics->push_back(ImportDiagnostic {
        .severity = ImportSeverity::kError,
        .code = std::move(code),
        .message = std::move(message),
        .source_path = this->source_path,
      });
    }

    auto ErrorAt(std::string code, std::string message,
      const nlohmann::json::json_pointer& location) const -> void
    {
      this->diagnostics->push_back(ImportDiagnostic {
        .severity = ImportSeverity::kError,
        .code = std::move(code),
        .message = std::move(message),
        .source_path = this->source_path,
        .json_pointer = location.to_string(),
      });
    }
  };

  auto CountErrors(const std::vector<ImportDiagnostic>& diagnostics) -> size_t
  {
    return static_cast<size_t>(std::ranges::count_if(
      diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.severity == ImportSeverity::kError;
      }));
  }

  auto PhysicsSchemaValidator() -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator = internal::MakeJsonSchemaValidator(
      nlohmann::json::parse(kPhysicsSidecarSchema));
    return validator;
  }

  auto ValidatePhysicsSchema(
    const nlohmann::json& doc, const Reporter& reporter) -> bool
  {
    constexpr auto kSchemaDiagConfig
      = internal::JsonSchemaValidationDiagnosticConfig {
          .validation_failed_code
          = import_codes::kPhysicsSidecarSchemaValidationFailed,
          .validation_failed_prefix = "Schema validation failed: ",
          .validator_failure_code
          = import_codes::kPhysicsSidecarSchemaValidatorFailure,
          .validator_failure_prefix
          = "Physics schema validation engine failure: ",
        };

    return internal::ValidateJsonSchemaWithDiagnostics(PhysicsSchemaValidator(),
      doc, kSchemaDiagConfig,
      [&](const std::string_view code, std::string message,
        std::string json_pointer) -> void {
        reporter.diagnostics->push_back(ImportDiagnostic {
          .severity = ImportSeverity::kError,
          .code = std::string(code),
          .message = std::move(message),
          .source_path = reporter.source_path,
          .json_pointer = std::move(json_pointer),
        });
      });
  }

  auto ReadRequiredString(
    const nlohmann::json& obj, const char* field, std::string& out) -> bool
  {
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

  auto ReadRequiredUInt16(const nlohmann::json& obj, const char* field,
    uint16_t& out, std::string& error) -> bool
  {
    (void)error;
    out = obj.at(field).get<uint16_t>();
    return true;
  }

  auto ReadRequiredFloat(const nlohmann::json& obj, const char* field,
    float& out, std::string& error) -> bool
  {
    (void)error;
    out = obj.at(field).get<float>();
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
      if (raw > std::numeric_limits<uint32_t>::max()) {
        error = "node_index_b is out of uint32 range";
        return false;
      }
      out = static_cast<uint32_t>(raw);
      return true;
    }
    error = "node_index_b must be uint32 or \"world\"";
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
    if (raw > std::numeric_limits<uint8_t>::max()) {
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
    std::span<float, 3> out, uint32_t& has_override, std::string& error) -> bool
  {
    (void)error;
    if (!obj.contains(field)) {
      return true;
    }
    const auto values = obj.at(field).get<std::array<float, 3>>();
    out[0] = values.at(0);
    out[1] = values.at(1);
    out[2] = values.at(2);
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

  auto ParseVehicleControllerType(const nlohmann::json& obj,
    phys::VehicleControllerType& out, std::string& error) -> bool
  {
    if (!obj.contains("controller_type")) {
      error = "Field 'controller_type' is required";
      return false;
    }
    const auto value = obj.at("controller_type").get<std::string>();
    if (value == "wheeled") {
      out = phys::VehicleControllerType::kWheeled;
      return true;
    }
    if (value == "tracked") {
      out = phys::VehicleControllerType::kTracked;
      return true;
    }
    error = "Field 'controller_type' has unsupported value";
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

  auto ParseBackendTargetString(
    const std::string_view value, PhysicsBackend& out) -> bool
  {
    if (value == "jolt") {
      out = PhysicsBackend::kJolt;
      return true;
    }
    if (value == "physx") {
      out = PhysicsBackend::kPhysX;
      return true;
    }
    return false;
  }

  [[nodiscard]] auto ParseBackendTargetField(
    const nlohmann::json& backend, PhysicsBackend& out) -> bool
  {
    const auto target = backend.at("target").get<std::string>();
    return ParseBackendTargetString(target, out);
  }

  auto ParseRigidBodyBackend(const nlohmann::json& obj,
    phys::RigidBodyBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    auto target = PhysicsBackend::kNone;
    if (!ParseBackendTargetField(backend, target)) {
      error = "Field 'backend.target' has unsupported value";
      return false;
    }
    if (target == PhysicsBackend::kJolt) {
      return ReadOptionalUInt8(backend, "num_velocity_steps_override",
               out.backend_scalars.jolt.num_velocity_steps_override, error)
        && ReadOptionalUInt8(backend, "num_position_steps_override",
          out.backend_scalars.jolt.num_position_steps_override, error);
    }
    if (target == PhysicsBackend::kPhysX) {
      return ReadOptionalUInt8(backend, "min_velocity_iters",
               out.backend_scalars.physx.min_velocity_iters, error)
        && ReadOptionalUInt8(backend, "min_position_iters",
          out.backend_scalars.physx.min_position_iters, error)
        && ReadOptionalFloat(backend, "max_contact_impulse",
          out.backend_scalars.physx.max_contact_impulse, error)
        && ReadOptionalFloat(backend, "contact_report_threshold",
          out.backend_scalars.physx.contact_report_threshold, error);
    }
    return false;
  }

  auto ParseCharacterBackend(const nlohmann::json& obj,
    phys::CharacterBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    auto target = PhysicsBackend::kNone;
    if (!ParseBackendTargetField(backend, target)) {
      error = "Field 'backend.target' has unsupported value";
      return false;
    }
    if (target == PhysicsBackend::kJolt) {
      return ReadOptionalFloat(backend, "penetration_recovery_speed",
               out.backend_scalars.jolt.penetration_recovery_speed, error)
        && ReadOptionalUInt32(
          backend, "max_num_hits", out.backend_scalars.jolt.max_num_hits, error)
        && ReadOptionalFloat(backend, "hit_reduction_cos_max_angle",
          out.backend_scalars.jolt.hit_reduction_cos_max_angle, error);
    }
    if (target == PhysicsBackend::kPhysX) {
      return ReadOptionalFloat(backend, "contact_offset",
        out.backend_scalars.physx.contact_offset, error);
    }
    return false;
  }

  auto ParseSoftBodyBackend(const nlohmann::json& obj,
    phys::SoftBodyBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    auto target = PhysicsBackend::kNone;
    if (!ParseBackendTargetField(backend, target)) {
      error = "Field 'backend.target' has unsupported value";
      return false;
    }
    if (target == PhysicsBackend::kJolt) {
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
    if (target == PhysicsBackend::kPhysX) {
      out.topology_format
        = phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary;
      return ReadOptionalFloat(backend, "youngs_modulus",
               out.backend_scalars.physx.youngs_modulus, error)
        && ReadOptionalFloat(
          backend, "poisson_ratio", out.backend_scalars.physx.poissons, error)
        && ReadOptionalFloat(backend, "dynamic_friction",
          out.backend_scalars.physx.dynamic_friction, error);
    }
    return false;
  }

  auto ParseJointBackend(const nlohmann::json& obj,
    phys::JointBindingRecord& out, std::string& error) -> bool
  {
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    auto target = PhysicsBackend::kNone;
    if (!ParseBackendTargetField(backend, target)) {
      error = "Field 'backend.target' has unsupported value";
      return false;
    }
    if (target == PhysicsBackend::kJolt) {
      return ReadOptionalUInt8(backend, "num_velocity_steps_override",
               out.backend_scalars.jolt.num_velocity_steps_override, error)
        && ReadOptionalUInt8(backend, "num_position_steps_override",
          out.backend_scalars.jolt.num_position_steps_override, error);
    }
    if (target == PhysicsBackend::kPhysX) {
      return ReadOptionalFloat(backend, "inv_mass_scale0",
               out.backend_scalars.physx.inv_mass_scale0, error)
        && ReadOptionalFloat(backend, "inv_mass_scale1",
          out.backend_scalars.physx.inv_mass_scale1, error)
        && ReadOptionalFloat(backend, "inv_inertia_scale0",
          out.backend_scalars.physx.inv_inertia_scale0, error)
        && ReadOptionalFloat(backend, "inv_inertia_scale1",
          out.backend_scalars.physx.inv_inertia_scale1, error);
    }
    return false;
  }

  auto ParseVehicleWheelBackend(const nlohmann::json& obj,
    phys::VehicleWheelBackendScalars& out, PhysicsBackend& out_target,
    std::string& error) -> bool
  {
    out_target = PhysicsBackend::kNone;
    if (!obj.contains("backend")) {
      return true;
    }
    const auto& backend = obj.at("backend");
    if (!ParseBackendTargetField(backend, out_target)) {
      error = "Field 'backend.target' has unsupported value";
      return false;
    }
    if (out_target == PhysicsBackend::kJolt) {
      return ReadOptionalFloat(
        backend, "wheel_castor", out.jolt.wheel_castor, error);
    }
    if (out_target == PhysicsBackend::kPhysX) {
      return true;
    }
    return false;
  }

  auto ParseRigidBodyBinding(const nlohmann::json& binding,
    RigidBodyBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref)) {
      return false;
    }
    if (!ReadRequiredString(binding, "material_ref", out.material_ref)) {
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
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref)) {
      return false;
    }
    if (!ReadRequiredString(binding, "material_ref", out.material_ref)) {
      return false;
    }

    out.record.node_index = node_index;
    auto is_sensor = false;
    if (!ReadOptionalUInt16(
          binding, "collision_layer", out.record.collision_layer, error)
      || !ReadOptionalUInt32(
        binding, "collision_mask", out.record.collision_mask, error)
      || !ReadOptionalBool(binding, "is_sensor", is_sensor, error)) {
      return false;
    }
    out.record.is_sensor = is_sensor ? 1U : 0U;
    return true;
  }

  auto ParseCharacterBinding(const nlohmann::json& binding,
    CharacterBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(binding, "shape_ref", out.shape_ref)) {
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
    if (!ReadRequiredString(binding, "source_mesh_ref", out.source_mesh_ref)) {
      return false;
    }
    out.record.node_index = node_index;
    out.backend_explicit = binding.contains("backend");
    auto self_collision = false;
    if (!ReadOptionalBool(binding, "self_collision", self_collision, error)) {
      return false;
    }
    out.record.self_collision = self_collision ? 1U : 0U;
    out.authored_binding = binding;

    return ReadOptionalString(
             binding, "collision_mesh_ref", out.collision_mesh_ref, error)
      && ReadRequiredUInt16(
        binding, "collision_layer", out.record.collision_layer, error)
      && ReadRequiredUInt32(
        binding, "collision_mask", out.record.collision_mask, error)
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
      || !ReadJointNodeIndexB(binding, node_index_b, error)) {
      return false;
    }
    out.record.node_index_a = node_index_a;
    out.record.node_index_b = node_index_b;
    if (!ParseJointBackend(binding, out.record, error)) {
      return false;
    }
    out.backend_explicit = binding.contains("backend");
    out.constraint_format = phys::PhysicsResourceFormat::kJoltConstraintBinary;
    if (binding.contains("backend")) {
      auto target = PhysicsBackend::kNone;
      if (!ParseBackendTargetField(binding.at("backend"), target)) {
        error = "Field 'backend.target' has unsupported value";
        return false;
      }
      if (target == PhysicsBackend::kPhysX) {
        out.constraint_format
          = phys::PhysicsResourceFormat::kPhysXConstraintBinary;
      }
    }
    out.authored_binding = binding;
    return true;
  }

  auto ParseVehicleBinding(const nlohmann::json& binding,
    VehicleBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)
      || !ParseVehicleControllerType(
        binding, out.record.controller_type, error)) {
      return false;
    }
    out.record.node_index = node_index;
    out.authored_binding = binding;
    out.backend_explicit = false;

    const auto& wheels = binding.at("wheels");
    out.wheels.clear();
    out.wheels.reserve(wheels.size());
    for (const auto& wheel : wheels) {
      auto wheel_source = VehicleWheelSource {};
      uint32_t wheel_node_index = 0;
      uint32_t axle_index_u32 = 0;
      if (!ReadRequiredUInt32(wheel, "node_index", wheel_node_index, error)
        || !ReadRequiredUInt32(wheel, "axle_index", axle_index_u32, error)
        || axle_index_u32 > std::numeric_limits<uint16_t>::max()
        || !ParseWheelSide(wheel, wheel_source.side, error)
        || !ParseVehicleWheelBackend(wheel, wheel_source.backend_scalars,
          wheel_source.backend_target, error)) {
        if (axle_index_u32 > std::numeric_limits<uint16_t>::max()) {
          error = "Field 'axle_index' exceeds uint16 range";
        }
        return false;
      }
      wheel_source.node_index = wheel_node_index;
      wheel_source.axle_index = static_cast<uint16_t>(axle_index_u32);
      out.backend_explicit = out.backend_explicit
        || wheel_source.backend_target != PhysicsBackend::kNone;
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
    std::vector<RecordT>& out, const Reporter& reporter,
    const ParseFn& parse_fn) -> void
  {
    if (!bindings.contains(key)) {
      return;
    }

    const auto& array = bindings.at(key);
    out.reserve(array.size());
    for (size_t i = 0; i < array.size(); ++i) {
      const auto location
        = nlohmann::json::json_pointer {} / "bindings" / std::string(key) / i;
      auto record = RecordT {};
      auto error = std::string {};
      if (!parse_fn(array.at(i), record, error)) {
        reporter.ErrorAt(
          std::string(import_codes::kPhysicsSidecarSchemaContractMismatch),
          std::move(error), location);
        continue;
      }
      out.push_back(std::move(record));
    }
  }

  auto ValidateSingletonPerNode(const std::span<const uint32_t> node_indices,
    std::string_view category, const Reporter& reporter) -> void
  {
    auto seen = std::unordered_map<uint32_t, size_t> {};
    for (size_t i = 0; i < node_indices.size(); ++i) {
      const auto [it, inserted]
        = seen.emplace(oxygen::base::CheckedAt(node_indices, i), i);
      if (!inserted) {
        reporter.ErrorAt(
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
          "Duplicate singleton binding for node "
            + std::to_string(oxygen::base::CheckedAt(node_indices, i)) + " in "
            + std::string(category),
          nlohmann::json::json_pointer {} / "bindings" / std::string(category)
            / i);
      }
    }
  }

  template <typename RecordT, typename NodeIndexFn>
  auto ValidateSingletonBindings(const std::vector<RecordT>& records,
    std::string_view category, const NodeIndexFn& node_index_of,
    const Reporter& reporter) -> void
  {
    auto singleton_nodes = std::vector<uint32_t> {};
    singleton_nodes.reserve(records.size());
    for (const auto& record : records) {
      singleton_nodes.push_back(node_index_of(record));
    }
    ValidateSingletonPerNode(singleton_nodes, category, reporter);
  }

  //! A soft body scalar that must be finite, and not negative unless
  //! `allow_negative`.
  struct SoftBodyScalarRule final {
    std::string_view field;
    float phys::SoftBodyBindingRecord::* member;
    bool allow_negative;
  };

  constexpr auto kSoftBodyScalarRules = std::array {
    SoftBodyScalarRule {
      .field = "edge_compliance",
      .member = &phys::SoftBodyBindingRecord::edge_compliance,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "shear_compliance",
      .member = &phys::SoftBodyBindingRecord::shear_compliance,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "bend_compliance",
      .member = &phys::SoftBodyBindingRecord::bend_compliance,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "volume_compliance",
      .member = &phys::SoftBodyBindingRecord::volume_compliance,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "pressure_coefficient",
      .member = &phys::SoftBodyBindingRecord::pressure_coefficient,
      .allow_negative = true,
    },
    SoftBodyScalarRule {
      .field = "tether_max_distance_multiplier",
      .member = &phys::SoftBodyBindingRecord::tether_max_distance_multiplier,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "global_damping",
      .member = &phys::SoftBodyBindingRecord::global_damping,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "restitution",
      .member = &phys::SoftBodyBindingRecord::restitution,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "friction",
      .member = &phys::SoftBodyBindingRecord::friction,
      .allow_negative = false,
    },
    SoftBodyScalarRule {
      .field = "vertex_radius",
      .member = &phys::SoftBodyBindingRecord::vertex_radius,
      .allow_negative = false,
    },
  };

  //! Reports the first rule the soft body breaks; true when it breaks none.
  auto ValidateSoftBodyScalars(const SoftBodyBindingSource& source,
    const nlohmann::json::json_pointer& base_path, const Reporter& reporter)
    -> bool
  {
    for (const auto& rule : kSoftBodyScalarRules) {
      const float value = source.record.*rule.member;
      const auto valid
        = std::isfinite(value) && (rule.allow_negative || value >= 0.0F);
      if (!valid) {
        reporter.ErrorAt(
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
          "soft_bodies." + std::string(rule.field) + " must be finite"
            + (rule.allow_negative ? "" : " and >= 0"),
          base_path / std::string(rule.field));
        return false;
      }
    }
    if (source.record.solver_iteration_count == 0U) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "soft_bodies.solver_iteration_count must be greater than zero",
        base_path / "solver_iteration_count");
      return false;
    }
    constexpr auto kMaxVertices
      = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
    if (source.pinned_vertices.size() > kMaxVertices) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "soft_bodies.pinned_vertices exceeds uint32 range",
        base_path / "pinned_vertices");
      return false;
    }
    if (source.kinematic_vertices.size() > kMaxVertices) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "soft_bodies.kinematic_vertices exceeds uint32 range",
        base_path / "kinematic_vertices");
      return false;
    }
    return true;
  }

  auto ValidateSoftBodies(std::vector<SoftBodyBindingSource>& records,
    const Reporter& reporter) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      auto& source = records.at(i);
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "soft_bodies" / i;
      if (!ValidateSoftBodyScalars(source, base_path, reporter)) {
        continue;
      }
      source.record.pinned_vertex_count
        = static_cast<uint32_t>(source.pinned_vertices.size());
      source.record.kinematic_vertex_count
        = static_cast<uint32_t>(source.kinematic_vertices.size());
    }
  }

  //! Reports the first rule the wheel breaks.
  auto ValidateVehicleWheel(const VehicleBindingSource& vehicle,
    const VehicleWheelSource& wheel,
    const nlohmann::json::json_pointer& wheel_path,
    std::unordered_set<uint32_t>& wheel_nodes,
    std::unordered_set<uint32_t>& wheel_roles, const Reporter& reporter) -> void
  {
    if (wheel.node_index == vehicle.record.node_index) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "Vehicle wheel node_index must differ from chassis node_index",
        wheel_path / "node_index");
      return;
    }
    if (!wheel_nodes.insert(wheel.node_index).second) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "Vehicle wheels must reference distinct node_index values",
        wheel_path / "node_index");
      return;
    }
    // The role packs the axle and the side: (axle << 16) | side.
    const auto role_key = (static_cast<uint32_t>(wheel.axle_index) << 16U)
      | static_cast<uint32_t>(wheel.side);
    if (!wheel_roles.insert(role_key).second) {
      reporter.ErrorAt(std::string(import_codes::kPhysicsSidecarPayloadInvalid),
        "Vehicle wheel role (axle_index + side) must be unique", wheel_path);
      return;
    }
  }

  auto ValidateVehicles(const std::vector<VehicleBindingSource>& records,
    const Reporter& reporter) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto& vehicle = records.at(i);
      const auto base_path
        = nlohmann::json::json_pointer {} / "bindings" / "vehicles" / i;
      if (vehicle.wheels.size() < 2U) {
        reporter.ErrorAt(
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
          "Vehicle must declare at least two wheels", base_path / "wheels");
        continue;
      }
      if (vehicle.wheels.size()
        > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        reporter.ErrorAt(
          std::string(import_codes::kPhysicsSidecarPayloadInvalid),
          "Vehicle wheel count exceeds uint32 range", base_path / "wheels");
        continue;
      }

      auto wheel_nodes = std::unordered_set<uint32_t> {};
      auto wheel_roles = std::unordered_set<uint32_t> {};
      for (size_t w = 0; w < vehicle.wheels.size(); ++w) {
        ValidateVehicleWheel(vehicle, vehicle.wheels.at(w),
          base_path / "wheels" / w, wheel_nodes, wheel_roles, reporter);
      }
    }
  }

  auto ParseBindings(const nlohmann::json& doc, PhysicsSidecarDocument& parsed,
    const Reporter& reporter) -> void
  {
    const auto& bindings = doc.at("bindings");
    ParseBindingsArray(bindings, "rigid_bodies", parsed.rigid_bodies, reporter,
      ParseRigidBodyBinding);
    ParseBindingsArray(
      bindings, "colliders", parsed.colliders, reporter, ParseColliderBinding);
    ParseBindingsArray(bindings, "characters", parsed.characters, reporter,
      ParseCharacterBinding);
    ParseBindingsArray(bindings, "soft_bodies", parsed.soft_bodies, reporter,
      ParseSoftBodyBinding);
    ParseBindingsArray(
      bindings, "joints", parsed.joints, reporter, ParseJointBinding);
    ParseBindingsArray(
      bindings, "vehicles", parsed.vehicles, reporter, ParseVehicleBinding);
    ParseBindingsArray(bindings, "aggregates", parsed.aggregates, reporter,
      ParseAggregateBinding);

    ValidateSingletonBindings(
      parsed.rigid_bodies, "rigid_bodies",
      [](const auto& record) -> auto { return record.record.node_index; },
      reporter);
    ValidateSingletonBindings(
      parsed.colliders, "colliders",
      [](const auto& record) -> auto { return record.record.node_index; },
      reporter);
    ValidateSingletonBindings(
      parsed.characters, "characters",
      [](const auto& record) -> auto { return record.record.node_index; },
      reporter);
    ValidateSingletonBindings(
      parsed.soft_bodies, "soft_bodies",
      [](const auto& record) -> auto { return record.record.node_index; },
      reporter);
    ValidateSingletonBindings(
      parsed.vehicles, "vehicles",
      [](const auto& record) -> auto { return record.record.node_index; },
      reporter);
    ValidateSingletonBindings(
      parsed.aggregates, "aggregates",
      [](const auto& record) -> auto { return record.node_index; }, reporter);

    ValidateSoftBodies(parsed.soft_bodies, reporter);
    ValidateVehicles(parsed.vehicles, reporter);
  }

} // namespace

auto PreparePhysicsSidecarDocument(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<PhysicsSidecarDocument>
{
  const auto reporter = Reporter {
    .diagnostics = make_observer(&diagnostics),
    .source_path = source_path.string(),
  };
  const auto errors_before = CountErrors(diagnostics);

  auto doc = nlohmann::json {};
  try {
    doc = nlohmann::json::parse(bytes);
  } catch (const std::exception& ex) {
    reporter.Error(std::string(import_codes::kPhysicsSidecarPayloadParseFailed),
      "Failed to parse sidecar document as JSON: " + std::string(ex.what()));
    return std::nullopt;
  }

  if (!ValidatePhysicsSchema(doc, reporter)) {
    return std::nullopt;
  }

  auto parsed = PhysicsSidecarDocument {};
  try {
    ParseBindings(doc, parsed, reporter);
  } catch (const std::exception& ex) {
    reporter.Error(
      std::string(import_codes::kPhysicsSidecarSchemaContractMismatch),
      "Schema validated sidecar document failed invariant extraction: "
        + std::string(ex.what()));
    return std::nullopt;
  }

  if (CountErrors(diagnostics) > errors_before) {
    return std::nullopt;
  }
  return parsed;
}

auto PhysicsSidecarReferences(const nlohmann::json& document)
  -> std::vector<PhysicsSidecarReference>
{
  struct Field final {
    std::string_view name;
    bool geometry = false;
  };
  constexpr auto kFields = std::array {
    Field { .name = "shape_ref" },
    Field { .name = "inner_shape_ref" },
    Field { .name = "material_ref" },
    Field { .name = "source_mesh_ref", .geometry = true },
    Field { .name = "collision_mesh_ref", .geometry = true },
  };
  auto references = std::vector<PhysicsSidecarReference> {};
  const auto bindings = document.find("bindings");
  if (bindings == document.end() || !bindings->is_object()) {
    return references;
  }
  for (const auto& [kind, rows] : bindings->items()) {
    if (!rows.is_array()) {
      continue;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto& row = rows.at(index);
      if (!row.is_object()) {
        continue;
      }
      const auto location
        = nlohmann::json::json_pointer {} / "bindings" / kind / index;
      for (const auto& field : kFields) {
        const auto value = row.find(field.name);
        if (value != row.end() && value->is_string()) {
          references.push_back(PhysicsSidecarReference {
            .written = value->get<std::string>(),
            .geometry = field.geometry,
            .json_pointer = (location / std::string { field.name }).to_string(),
          });
        }
      }
    }
  }
  return references;
}

} // namespace oxygen::content::import
