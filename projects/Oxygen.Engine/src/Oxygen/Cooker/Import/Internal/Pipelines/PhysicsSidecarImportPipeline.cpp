//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Jolt/Jolt.h> // Must always be first (keep separate)

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
#include <mutex>
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

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Jolt/Core/Memory.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {
namespace {

  namespace phys = data::pak::physics;
  namespace lc = oxygen::content::lc;
  using PhysicsBackend = core::meta::physics::PhysicsBackend;
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

  auto PatchContentHash(std::vector<std::byte>& bytes,
    const data::pak::core::ContentHashDigest& hash) -> void
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
    data::AssetKey source_mesh_asset_key {};
    std::string source_mesh_descriptor_relpath;
    std::optional<std::string> collision_mesh_ref;
    std::vector<uint32_t> pinned_vertices;
    std::vector<uint32_t> kinematic_vertices;
    bool backend_explicit { false };
    nlohmann::json authored_binding;
  };

  struct VehicleWheelSource final {
    uint32_t node_index { 0 };
    uint16_t axle_index { 0 };
    phys::VehicleWheelSide side { phys::VehicleWheelSide::kLeft };
    phys::VehicleWheelBackendScalars backend_scalars {};
    PhysicsBackend backend_target { PhysicsBackend::kNone };
  };

  struct JointBindingSource final {
    phys::JointBindingRecord record {};
    phys::PhysicsResourceFormat constraint_format
      = phys::PhysicsResourceFormat::kJoltConstraintBinary;
    bool backend_explicit { false };
    nlohmann::json authored_binding;
  };

  struct VehicleBindingSource final {
    phys::VehicleBindingRecord record {};
    std::vector<VehicleWheelSource> wheels;
    phys::PhysicsResourceFormat constraint_format
      = phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary;
    bool backend_explicit { false };
    nlohmann::json authored_binding;
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

  [[nodiscard]] auto BackendForVehicleFormat(
    const phys::PhysicsResourceFormat format) -> std::optional<PhysicsBackend>
  {
    switch (format) {
    case phys::PhysicsResourceFormat::kJoltVehicleConstraintBinary:
      return PhysicsBackend::kJolt;
    case phys::PhysicsResourceFormat::kPhysXVehicleSettingsBinary:
      return PhysicsBackend::kPhysX;
    default:
      return std::nullopt;
    }
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
    for (size_t i = 0; i < wheels.size(); ++i) {
      const auto& wheel = wheels[i];
      auto wheel_source = VehicleWheelSource {};
      uint32_t wheel_node_index = 0;
      uint32_t axle_index_u32 = 0;
      if (!ReadRequiredUInt32(wheel, "node_index", wheel_node_index, error)
        || !ReadRequiredUInt32(wheel, "axle_index", axle_index_u32, error)
        || axle_index_u32 > (std::numeric_limits<uint16_t>::max)()
        || !ParseWheelSide(wheel, wheel_source.side, error)
        || !ParseVehicleWheelBackend(wheel, wheel_source.backend_scalars,
          wheel_source.backend_target, error)) {
        if (axle_index_u32 > (std::numeric_limits<uint16_t>::max)()) {
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

  struct ResolvedAssetRecord final {
    data::AssetType type = data::AssetType::kUnknown;
    std::string descriptor_relpath;
  };

  using AssetRecordMap
    = std::unordered_map<data::AssetKey, ResolvedAssetRecord>;

  auto BuildAssetRecordMap(const lc::Inspection& inspection) -> AssetRecordMap
  {
    auto out = AssetRecordMap {};
    const auto assets = inspection.Assets();
    out.reserve(assets.size());
    for (const auto& asset : assets) {
      out.insert_or_assign(asset.key,
        ResolvedAssetRecord {
          .type = static_cast<data::AssetType>(asset.asset_type),
          .descriptor_relpath = asset.descriptor_relpath,
        });
    }
    return out;
  }

  struct BindingValidationContext final {
    ImportSession& session;
    const ImportRequest& request;
    content::VirtualPathResolver& resolver;
    std::span<const SidecarCookedInspectionContext> cooked_contexts;
    const AssetRecordMap& target_assets;
    const std::filesystem::path& target_cooked_root;
    uint32_t node_count = 0;
  };

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

    const auto target_it = ctx.target_assets.find(*resolved_key);
    if (target_it == ctx.target_assets.end()) {
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

    if (target_it->second.type != expected_type) {
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
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt
      = SoftBodyFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(ctx.session, ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for soft-body topology "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

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

      const auto source_mesh_key
        = ResolveAssetKeyForVirtualPath(ctx, records[i].source_mesh_ref,
          base_path + ".source_mesh_ref", data::AssetType::kGeometry,
          "physics.sidecar.source_mesh_ref_unresolved",
          "physics.sidecar.source_mesh_ref_not_geometry");
      if (!source_mesh_key.has_value()) {
        continue;
      }
      records[i].source_mesh_asset_key = *source_mesh_key;
      const auto source_mesh_entry = ctx.target_assets.find(*source_mesh_key);
      if (source_mesh_entry == ctx.target_assets.end()
        || source_mesh_entry->second.descriptor_relpath.empty()) {
        AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
          "physics.sidecar.source_mesh_ref_unresolved",
          "Resolved source mesh has no descriptor path in target cooked root",
          base_path + ".source_mesh_ref");
        continue;
      }
      records[i].source_mesh_descriptor_relpath
        = source_mesh_entry->second.descriptor_relpath;

      if (records[i].collision_mesh_ref.has_value()) {
        (void)ResolveAssetKeyForVirtualPath(ctx, *records[i].collision_mesh_ref,
          base_path + ".collision_mesh_ref", data::AssetType::kGeometry,
          "physics.sidecar.collision_mesh_ref_unresolved",
          "physics.sidecar.collision_mesh_ref_not_geometry");
        if (requested_backend == PhysicsBackend::kJolt) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "soft_bodies.collision_mesh_ref is not supported for jolt backend",
            base_path + ".collision_mesh_ref");
          continue;
        }
      }

      if (records[i].backend_explicit) {
        const auto authored_backend
          = BackendForSoftBodyFormat(records[i].record.topology_format);
        if (!authored_backend.has_value()
          || *authored_backend != requested_backend) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.backend_mismatch",
            "soft-body backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            base_path + ".backend.target");
          continue;
        }
      }
      records[i].record.topology_format = *expected_format_opt;
    }
  }

  auto ResolveJointBindings(std::vector<JointBindingSource>& records,
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt = JointFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(ctx.session, ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for joint constraint "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

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

      if (records[i].backend_explicit) {
        const auto authored_backend
          = BackendForJointFormat(records[i].constraint_format);
        if (!authored_backend.has_value()
          || *authored_backend != requested_backend) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.backend_mismatch",
            "joint backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            base_path + ".backend.target");
          continue;
        }
      }
      records[i].constraint_format = *expected_format_opt;
    }
  }

  auto ResolveVehicleBindings(std::vector<VehicleBindingSource>& records,
    std::vector<phys::VehicleWheelBindingRecord>& wheel_records,
    const PhysicsBackend requested_backend, BindingValidationContext& ctx)
    -> void
  {
    const auto expected_format_opt = VehicleFormatForBackend(requested_backend);
    if (!expected_format_opt.has_value()) {
      AddDiagnostic(ctx.session, ctx.request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend is not supported for vehicle constraint "
        "cooking: "
          + std::string(BackendName(requested_backend)));
      return;
    }

    wheel_records.clear();
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.vehicles[") + std::to_string(i) + "]";
      if (!ValidateNodeIndex(records[i].record.node_index, ctx.node_count,
            ctx.session, ctx.request, base_path + ".node_index")) {
        continue;
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
      auto slice_count = uint32_t { 0 };
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
        if (slice_count == (std::numeric_limits<uint32_t>::max)()) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.payload_invalid",
            "Vehicle wheel count exceeds uint32 range", wheel_path);
          continue;
        }
        if (wheel.backend_target != PhysicsBackend::kNone
          && wheel.backend_target != requested_backend) {
          AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
            "physics.sidecar.backend_mismatch",
            "vehicle wheel backend.target must match requested import backend "
              + std::string(BackendName(requested_backend)),
            wheel_path + ".backend.target");
          continue;
        }

        wheel_records.push_back(phys::VehicleWheelBindingRecord {
          .vehicle_node_index = records[i].record.node_index,
          .wheel_node_index = wheel.node_index,
          .axle_index = wheel.axle_index,
          .side = wheel.side,
          .backend_scalars = wheel.backend_scalars,
        });
        slice_count += 1U;
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
      records[i].constraint_format = *expected_format_opt;
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
    std::call_once(once, [] { JPH::RegisterDefaultAllocator(); });
  }

  [[nodiscard]] auto JsonFloatOr(
    const nlohmann::json& obj, const char* key, const float fallback) -> float
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    return obj.at(key).get<float>();
  }

  [[nodiscard]] auto JsonBoolOr(
    const nlohmann::json& obj, const char* key, const bool fallback) -> bool
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    return obj.at(key).get<bool>();
  }

  [[nodiscard]] auto JsonVec3Or(const nlohmann::json& obj, const char* key,
    const JPH::Vec3& fallback) -> JPH::Vec3
  {
    if (!obj.contains(key)) {
      return fallback;
    }
    const auto values = obj.at(key).get<std::array<float, 3>>();
    return JPH::Vec3(values[0], values[1], values[2]);
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
      out_curve.AddPoint(point[0].get<float>(), point[1].get<float>());
    }
    out_curve.Sort();
    return true;
  }

  auto SerializeJoltConstraintSettings(const JPH::ConstraintSettings& settings,
    std::vector<std::byte>& out_bytes) -> bool
  {
    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    out_bytes.assign(reinterpret_cast<const std::byte*>(blob.data()),
      reinterpret_cast<const std::byte*>(blob.data()) + blob.size());
    return true;
  }

  auto SerializeJoltVehicleSettings(
    const JPH::VehicleConstraintSettings& settings,
    std::vector<std::byte>& out_bytes) -> bool
  {
    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    out_bytes.assign(reinterpret_cast<const std::byte*>(blob.data()),
      reinterpret_cast<const std::byte*>(blob.data()) + blob.size());
    return true;
  }

  auto SerializeJoltSoftBodySettings(
    const JPH::SoftBodySharedSettings& settings, std::vector<std::byte>& out)
    -> bool
  {
    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    settings.SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return false;
    }
    const auto blob = stream.str();
    out.assign(reinterpret_cast<const std::byte*>(blob.data()),
      reinterpret_cast<const std::byte*>(blob.data()) + blob.size());
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
        settings.mMinDistance = limits[0];
      }
      if (authored.contains("limits_upper")) {
        const auto limits
          = authored.at("limits_upper").get<std::array<float, 6>>();
        settings.mMaxDistance = limits[0];
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
        settings.mHalfConeAngle = std::max(0.0F, limits[3]);
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
            lower[axis], upper[axis]);
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

  struct LoadedBufferResources final {
    std::vector<data::pak::core::BufferResourceDesc> table;
    std::vector<std::byte> data;
  };

  auto ReadBinaryFile(const std::filesystem::path& path,
    std::vector<std::byte>& out, std::string& error) -> bool
  {
    auto in = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
      error = "failed opening file '" + path.string() + "'";
      return false;
    }
    const auto end = in.tellg();
    if (end < 0) {
      error = "failed reading file size for '" + path.string() + "'";
      return false;
    }
    out.resize(static_cast<size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!out.empty()) {
      in.read(reinterpret_cast<char*>(out.data()),
        static_cast<std::streamsize>(out.size()));
      if (!(in.good() || in.eof())) {
        error = "failed reading file bytes from '" + path.string() + "'";
        return false;
      }
    }
    return true;
  }

  template <typename T>
  auto ReadPodAt(std::span<const std::byte> bytes, const size_t offset, T& out)
    -> bool
  {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
      return false;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
  }

  [[nodiscard]] auto DecodeFixedName(const char* raw_name) -> std::string
  {
    auto length = size_t { 0 };
    while (length < data::pak::core::kMaxNameSize && raw_name[length] != '\0') {
      ++length;
    }
    return std::string(raw_name, length);
  }

  auto ParseGeometryTopologyInput(std::span<const std::byte> descriptor_bytes,
    SoftBodyGeometryTopologyInput& out, std::string& error) -> bool
  {
    using data::MeshType;
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    auto asset_desc = GeometryAssetDesc {};
    if (!ReadPodAt(descriptor_bytes, 0U, asset_desc)) {
      error = "geometry descriptor is too small for GeometryAssetDesc";
      return false;
    }
    if (asset_desc.lod_count == 0U) {
      error = "geometry descriptor has lod_count=0";
      return false;
    }

    auto offset = sizeof(GeometryAssetDesc);
    auto mesh_desc = MeshDesc {};
    if (!ReadPodAt(descriptor_bytes, offset, mesh_desc)) {
      error = "geometry descriptor is too small for MeshDesc";
      return false;
    }
    offset += sizeof(MeshDesc);

    const auto mesh_type = static_cast<MeshType>(mesh_desc.mesh_type);
    if (mesh_type == MeshType::kStandard) {
      out.kind = SoftBodyGeometryTopologyInput::Kind::kStandard;
      out.vertex_buffer = mesh_desc.info.standard.vertex_buffer;
      out.index_buffer = mesh_desc.info.standard.index_buffer;
    } else if (mesh_type == MeshType::kProcedural) {
      out.kind = SoftBodyGeometryTopologyInput::Kind::kProcedural;
      out.procedural_name = DecodeFixedName(mesh_desc.name);
      const auto params_size
        = static_cast<size_t>(mesh_desc.info.procedural.params_size);
      if (offset > descriptor_bytes.size()
        || descriptor_bytes.size() - offset < params_size) {
        error = "geometry descriptor procedural params exceed descriptor size";
        return false;
      }
      out.procedural_params.assign(descriptor_bytes.begin() + offset,
        descriptor_bytes.begin() + offset + params_size);
      offset += params_size;
    } else {
      error = "soft-body source mesh must be standard or procedural geometry";
      return false;
    }

    out.mesh_views.clear();
    for (uint32_t submesh_index = 0; submesh_index < mesh_desc.submesh_count;
      ++submesh_index) {
      auto submesh_desc = SubMeshDesc {};
      if (!ReadPodAt(descriptor_bytes, offset, submesh_desc)) {
        error = "geometry descriptor submesh table exceeds descriptor size";
        return false;
      }
      offset += sizeof(SubMeshDesc);

      for (uint32_t view_index = 0; view_index < submesh_desc.mesh_view_count;
        ++view_index) {
        auto view_desc = MeshViewDesc {};
        if (!ReadPodAt(descriptor_bytes, offset, view_desc)) {
          error = "geometry descriptor mesh view table exceeds descriptor size";
          return false;
        }
        offset += sizeof(MeshViewDesc);
        out.mesh_views.push_back(view_desc);
      }
    }

    if (out.mesh_views.empty()) {
      error = "soft-body source mesh has no mesh views";
      return false;
    }
    return true;
  }

  auto LoadBufferResources(const std::filesystem::path& cooked_root,
    const LooseCookedLayout& layout, LoadedBufferResources& out,
    std::string& error) -> bool
  {
    auto table_bytes = std::vector<std::byte> {};
    auto data_bytes = std::vector<std::byte> {};
    const auto table_path
      = cooked_root / std::filesystem::path(layout.BuffersTableRelPath());
    const auto data_path
      = cooked_root / std::filesystem::path(layout.BuffersDataRelPath());
    if (!ReadBinaryFile(table_path, table_bytes, error)) {
      return false;
    }
    if (!ReadBinaryFile(data_path, data_bytes, error)) {
      return false;
    }

    if (table_bytes.size() % sizeof(data::pak::core::BufferResourceDesc)
      != 0U) {
      error = "buffers table has invalid size";
      return false;
    }

    out.table.resize(
      table_bytes.size() / sizeof(data::pak::core::BufferResourceDesc));
    if (!table_bytes.empty()) {
      std::memcpy(out.table.data(), table_bytes.data(), table_bytes.size());
    }
    out.data = std::move(data_bytes);
    return true;
  }

  auto ReadBufferPayload(const LoadedBufferResources& resources,
    const data::pak::core::ResourceIndexT index,
    data::pak::core::BufferResourceDesc& descriptor,
    std::span<const std::byte>& payload, std::string& error) -> bool
  {
    if (index == data::pak::core::kNoResourceIndex) {
      error = "geometry references kNoResourceIndex buffer";
      return false;
    }
    const auto table_index = static_cast<size_t>(static_cast<uint32_t>(index));
    if (table_index >= resources.table.size()) {
      error = "geometry references out-of-range buffer resource index";
      return false;
    }

    descriptor = resources.table[table_index];
    const auto offset = static_cast<size_t>(descriptor.data_offset);
    const auto size = static_cast<size_t>(descriptor.size_bytes);
    if (offset > resources.data.size()
      || resources.data.size() - offset < size) {
      error = "buffer payload range exceeds buffers.data size";
      return false;
    }

    payload = std::span<const std::byte>(resources.data.data() + offset, size);
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
      auto x = 0.0F;
      auto y = 0.0F;
      auto z = 0.0F;
      const auto* record = payload.data() + i * stride;
      std::memcpy(&x, record + 0U * sizeof(float), sizeof(float));
      std::memcpy(&y, record + 1U * sizeof(float), sizeof(float));
      std::memcpy(&z, record + 2U * sizeof(float), sizeof(float));
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        error = "vertex buffer contains non-finite positions";
        return false;
      }
      vertices.emplace_back(x, y, z);
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
    if (element_size == sizeof(uint32_t)) {
      for (size_t i = 0; i < count; ++i) {
        auto value = uint32_t { 0 };
        std::memcpy(
          &value, payload.data() + i * sizeof(uint32_t), sizeof(uint32_t));
        indices.push_back(value);
      }
    } else {
      for (size_t i = 0; i < count; ++i) {
        auto value = uint16_t { 0 };
        std::memcpy(
          &value, payload.data() + i * sizeof(uint16_t), sizeof(uint16_t));
        indices.push_back(static_cast<uint32_t>(value));
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
    return dx * dx + dy * dy + dz * dz;
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
    const auto cx = aby * acz - abz * acy;
    const auto cy = abz * acx - abx * acz;
    const auto cz = abx * acy - aby * acx;
    return cx * cx + cy * cy + cz * cz;
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

    const auto max_extent = std::max({ bounds_max.x - bounds_min.x,
      bounds_max.y - bounds_min.y, bounds_max.z - bounds_min.z });
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
        const auto local0 = indices[first_index + i + 0U];
        const auto local1 = indices[first_index + i + 1U];
        const auto local2 = indices[first_index + i + 2U];

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

        const auto& p0 = vertices[v0];
        const auto& p1 = vertices[v1];
        const auto& p2 = vertices[v2];
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

    if (!BuildFacesFromMeshViews(source_indices, topology.mesh_views,
          out.vertices, out.faces, error)) {
      return false;
    }
    return true;
  }

  auto BuildSurfaceMeshFromStandard(
    const SoftBodyGeometryTopologyInput& topology,
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout,
    SoftBodySurfaceMesh& out, std::string& error) -> bool
  {
    auto resources = LoadedBufferResources {};
    if (!LoadBufferResources(cooked_root, layout, resources, error)) {
      return false;
    }

    auto vertex_descriptor = data::pak::core::BufferResourceDesc {};
    auto index_descriptor = data::pak::core::BufferResourceDesc {};
    auto vertex_payload = std::span<const std::byte> {};
    auto index_payload = std::span<const std::byte> {};

    if (!ReadBufferPayload(resources, topology.vertex_buffer, vertex_descriptor,
          vertex_payload, error)) {
      return false;
    }
    if (!ReadBufferPayload(resources, topology.index_buffer, index_descriptor,
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
    const std::filesystem::path& target_cooked_root,
    const LooseCookedLayout& layout, SoftBodySurfaceMesh& out,
    std::string& error) -> bool
  {
    if (source.source_mesh_descriptor_relpath.empty()) {
      error = "soft-body source mesh descriptor path is unresolved";
      return false;
    }

    const auto descriptor_path = target_cooked_root
      / std::filesystem::path(source.source_mesh_descriptor_relpath);
    auto descriptor_bytes = std::vector<std::byte> {};
    if (!ReadBinaryFile(descriptor_path, descriptor_bytes, error)) {
      return false;
    }

    auto topology = SoftBodyGeometryTopologyInput {};
    if (!ParseGeometryTopologyInput(descriptor_bytes, topology, error)) {
      return false;
    }

    switch (topology.kind) {
    case SoftBodyGeometryTopologyInput::Kind::kStandard:
      return BuildSurfaceMeshFromStandard(
        topology, target_cooked_root, layout, out, error);
    case SoftBodyGeometryTopologyInput::Kind::kProcedural:
      return BuildSurfaceMeshFromProcedural(topology, out, error);
    }

    error = "soft-body source mesh kind is unsupported";
    return false;
  }

  constexpr float kMinCookedVolumeCompliance = 1.0e-6F;
  constexpr double kMinAbsCookedSurfaceRestVolume = 1.0e-8;

  [[nodiscard]] auto ComputeSignedSurfaceVolume(
    const JPH::SoftBodySharedSettings& settings) noexcept -> double
  {
    double signed_six_volume = 0.0;
    for (const auto& face : settings.mFaces) {
      const auto& p0 = settings.mVertices[face.mVertex[0]].mPosition;
      const auto& p1 = settings.mVertices[face.mVertex[1]].mPosition;
      const auto& p2 = settings.mVertices[face.mVertex[2]].mPosition;
      signed_six_volume += static_cast<double>(p0.x)
          * (static_cast<double>(p1.y) * static_cast<double>(p2.z)
            - static_cast<double>(p1.z) * static_cast<double>(p2.y))
        + static_cast<double>(p0.y)
          * (static_cast<double>(p1.z) * static_cast<double>(p2.x)
            - static_cast<double>(p1.x) * static_cast<double>(p2.z))
        + static_cast<double>(p0.z)
          * (static_cast<double>(p1.x) * static_cast<double>(p2.y)
            - static_cast<double>(p1.y) * static_cast<double>(p2.x));
    }
    return signed_six_volume / 6.0;
  }

  auto ValidateCookedSoftBodySettings(
    const JPH::SoftBodySharedSettings& settings,
    const float pressure_coefficient, std::string& error) -> bool
  {
    for (size_t i = 0; i < settings.mVolumeConstraints.size(); ++i) {
      const auto& volume = settings.mVolumeConstraints[i];
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
      const auto signed_volume = ComputeSignedSurfaceVolume(settings);
      if (!std::isfinite(signed_volume)
        || std::abs(signed_volume) < kMinAbsCookedSurfaceRestVolume) {
        error = "pressure_coefficient > 0 requires finite non-zero rest volume";
        return false;
      }
    }
    return true;
  }

  auto CookJoltSoftBodyBlob(const SoftBodyBindingSource& source,
    const std::filesystem::path& target_cooked_root,
    const LooseCookedLayout& layout, std::vector<std::byte>& out_blob,
    std::string& error) -> bool
  {
    EnsureJoltAllocatorReady();

    auto surface_mesh = SoftBodySurfaceMesh {};
    if (!BuildSoftBodySurfaceMesh(
          source, target_cooked_root, layout, surface_mesh, error)) {
      return false;
    }

    auto shared_settings = JPH::Ref<JPH::SoftBodySharedSettings> {
      new JPH::SoftBodySharedSettings()
    };
    if (shared_settings == nullptr) {
      error = "failed to allocate Jolt soft-body shared settings";
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

    const auto vertex_count
      = static_cast<uint32_t>(shared_settings->mVertices.size());
    for (const auto vertex_index : source.pinned_vertices) {
      if (vertex_index >= vertex_count) {
        error = "pinned_vertices contains out-of-range vertex index";
        return false;
      }
      shared_settings->mVertices[vertex_index].mInvMass = 0.0F;
    }
    for (const auto vertex_index : source.kinematic_vertices) {
      if (vertex_index >= vertex_count) {
        error = "kinematic_vertices contains out-of-range vertex index";
        return false;
      }
      shared_settings->mVertices[vertex_index].mInvMass = 0.0F;
    }

    shared_settings->CalculateEdgeLengths();
    shared_settings->CalculateBendConstraintConstants();
    shared_settings->CalculateVolumeConstraintVolumes();
    shared_settings->CalculateLRALengths(
      source.record.tether_max_distance_multiplier);
    if (!ValidateCookedSoftBodySettings(
          *shared_settings, source.record.pressure_coefficient, error)) {
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
      for (const auto ratio : transmission.at("forward_gear_ratios")) {
        out.mGearRatios.push_back(ratio.get<float>());
      }
    }
    if (transmission.contains("reverse_gear_ratios")) {
      out.mReverseGearRatios.clear();
      for (const auto ratio : transmission.at("reverse_gear_ratios")) {
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
      const auto& authored_wheel = authored_wheels[i];
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
        if (source.wheels[i].side == phys::VehicleWheelSide::kLeft) {
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
    const std::filesystem::path& target_cooked_root,
    const LooseCookedLayout& layout, std::vector<std::byte>& out_blob,
    std::string& error) -> bool
  {
    switch (source.record.topology_format) {
    case phys::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary:
      return CookJoltSoftBodyBlob(
        source, target_cooked_root, layout, out_blob, error);
    case phys::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary:
      error = "physx soft-body cooking is not implemented";
      return false;
    default:
      error = "soft-body topology format does not match any supported backend";
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
    const phys::PhysicsResourceFormat format, const bool with_hashing)
    -> CookedPhysicsResourcePayload
  {
    auto payload = CookedPhysicsResourcePayload {};
    payload.data.assign(blob_bytes.begin(), blob_bytes.end());
    payload.format = format;
    payload.alignment = 16;
    if (with_hashing) {
      payload.content_hash = util::ComputeContentSha256(payload.data);
    }
    return payload;
  }

  auto EmitBindingBlob(ImportSession& session, const ImportRequest& request,
    const std::string_view object_path,
    const std::vector<std::byte>& blob_bytes,
    const phys::PhysicsResourceFormat format, const bool with_hashing,
    data::AssetKey& out_asset_key) -> bool
  {
    try {
      const auto source
        = request.source_path.string() + "#" + std::string(object_path);
      const auto emitted = session.PhysicsResourceEmitter().Emit(
        BuildCookedPhysicsPayload(blob_bytes, format, with_hashing), source);
      out_asset_key = emitted.resource_asset_key;
      return true;
    } catch (const std::exception& ex) {
      AddDiagnosticAtPath(session, request, ImportSeverity::kError,
        "physics.sidecar.resource_emit_failed",
        "Failed emitting backend-cooked physics payload: "
          + std::string(ex.what()),
        std::string(object_path));
      return false;
    }
  }

  auto EmitSoftBodyTopologyResources(
    std::vector<SoftBodyBindingSource>& records, ImportSession& session,
    const ImportRequest& request,
    const std::filesystem::path& target_cooked_root,
    const LooseCookedLayout& layout) -> bool
  {
    const auto with_hashing
      = EffectiveContentHashingEnabled(request.options.with_content_hashing);
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookSoftBodyTopologyBlob(
            records[i], target_cooked_root, layout, blob_bytes, cook_error)) {
        LOG_F(ERROR,
          "PhysicsSidecarImportPipeline: soft-body topology cooking failed "
          "(source_mesh_ref='{}' reason='{}')",
          records[i].source_mesh_ref, cook_error);
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.resource_serialize_failed",
          "Failed to cook soft-body topology resource: " + cook_error,
          "bindings.soft_bodies[" + std::to_string(i) + "]");
        continue;
      }
      if (!EmitBindingBlob(session, request,
            "bindings.soft_bodies[" + std::to_string(i) + "]", blob_bytes,
            records[i].record.topology_format, with_hashing,
            records[i].record.topology_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitJointConstraintResources(std::vector<JointBindingSource>& records,
    ImportSession& session, const ImportRequest& request) -> bool
  {
    const auto with_hashing
      = EffectiveContentHashingEnabled(request.options.with_content_hashing);
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookJointConstraintBlob(records[i], blob_bytes, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.resource_serialize_failed",
          "Failed to cook joint constraint resource: " + cook_error,
          "bindings.joints[" + std::to_string(i) + "]");
        continue;
      }
      if (!EmitBindingBlob(session, request,
            "bindings.joints[" + std::to_string(i) + "]", blob_bytes,
            records[i].constraint_format, with_hashing,
            records[i].record.constraint_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitVehicleConstraintResources(
    std::vector<VehicleBindingSource>& records, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    const auto with_hashing
      = EffectiveContentHashingEnabled(request.options.with_content_hashing);
    for (size_t i = 0; i < records.size(); ++i) {
      auto blob_bytes = std::vector<std::byte> {};
      auto cook_error = std::string {};
      if (!CookVehicleConstraintBlob(records[i], blob_bytes, cook_error)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "physics.sidecar.resource_serialize_failed",
          "Failed to cook vehicle constraint resource: " + cook_error,
          "bindings.vehicles[" + std::to_string(i) + "]");
        continue;
      }
      if (!EmitBindingBlob(session, request,
            "bindings.vehicles[" + std::to_string(i) + "]", blob_bytes,
            records[i].constraint_format, with_hashing,
            records[i].record.constraint_asset_key)) {
        continue;
      }
    }
    return !session.HasErrors();
  }

  auto EmitBackendCookedBindingResources(PhysicsSidecarDocument& parsed,
    ImportSession& session, const ImportRequest& request,
    const std::filesystem::path& target_cooked_root,
    const LooseCookedLayout& layout) -> bool
  {
    return EmitSoftBodyTopologyResources(
             parsed.soft_bodies, session, request, target_cooked_root, layout)
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
    const auto scene_hash
      = base::ComputeSha256(scene_state.source_scene_descriptor);
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
      PatchContentHash(bytes, util::ComputeContentSha256(bytes));
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

    const auto target_assets = BuildAssetRecordMap(target_context->inspection);
    const auto requested_backend = ResolveRequestedPhysicsBackend(request);
    if (requested_backend == PhysicsBackend::kNone) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.sidecar.backend_unsupported",
        "Requested physics backend 'none' is not valid for sidecar cooking");
      return false;
    }
    auto validation_ctx = BindingValidationContext { session, request, resolver,
      cooked_contexts, target_assets, target_context->cooked_root,
      resolved_scene_state.node_count };

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
      [](const auto& record) { return record.node_index; }, validation_ctx);
    if (session.HasErrors()) {
      return false;
    }
    return EmitBackendCookedBindingResources(parsed, session, request,
      target_context->cooked_root, request.loose_cooked_layout);
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
    auto vehicle_sources = parsed.vehicles;
    std::ranges::sort(vehicle_sources, [](const auto& lhs, const auto& rhs) {
      return lhs.record.node_index < rhs.record.node_index;
    });
    auto vehicle_records = std::vector<phys::VehicleBindingRecord> {};
    auto wheel_records = std::vector<phys::VehicleWheelBindingRecord> {};
    vehicle_records.reserve(vehicle_sources.size());
    for (const auto& source : vehicle_sources) {
      auto record = source.record;
      if (wheel_records.size()
        > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        continue;
      }

      auto sorted_wheels = source.wheels;
      std::ranges::sort(sorted_wheels, [](const auto& lhs, const auto& rhs) {
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
    if (const auto wheel_table
      = MakeTableBlob(phys::PhysicsBindingType::kVehicleWheel, wheel_records);
      wheel_table.has_value()) {
      tables.push_back(*wheel_table);
    }
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
