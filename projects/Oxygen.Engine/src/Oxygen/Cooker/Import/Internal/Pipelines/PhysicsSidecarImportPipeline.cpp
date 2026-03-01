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
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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
#include <Oxygen/Cooker/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
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
    constexpr auto kSceneSuffix = std::string_view { ".oscene" };
    constexpr auto kPhysicsSuffix = std::string_view { ".physics" };

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

  auto ValidateNoDotSegments(const std::string_view path) -> bool
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == "." || segment == "..") {
        return false;
      }
      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
    return true;
  }

  auto IsCanonicalVirtualPath(const std::string_view virtual_path) -> bool
  {
    if (virtual_path.empty()) {
      return false;
    }
    if (virtual_path.front() != '/') {
      return false;
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      return false;
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      return false;
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      return false;
    }
    if (!ValidateNoDotSegments(virtual_path)) {
      return false;
    }
    return true;
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
    std::string shape_virtual_path;
    std::string material_virtual_path;
  };

  struct ColliderBindingSource final {
    phys::ColliderBindingRecord record {};
    std::string shape_virtual_path;
    std::string material_virtual_path;
  };

  struct CharacterBindingSource final {
    phys::CharacterBindingRecord record {};
    std::string shape_virtual_path;
  };

  struct PhysicsSidecarDocument final {
    std::vector<RigidBodyBindingSource> rigid_bodies;
    std::vector<ColliderBindingSource> colliders;
    std::vector<CharacterBindingSource> characters;
    std::vector<phys::SoftBodyBindingRecord> soft_bodies;
    std::vector<phys::JointBindingRecord> joints;
    std::vector<phys::VehicleBindingRecord> vehicles;
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

  auto ParseRigidBodyBinding(const nlohmann::json& binding,
    RigidBodyBindingSource& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    if (!ReadRequiredString(
          binding, "shape_virtual_path", out.shape_virtual_path, error)) {
      return false;
    }
    if (!ReadRequiredString(
          binding, "material_virtual_path", out.material_virtual_path, error)) {
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
        binding, "gravity_factor", out.record.gravity_factor, error)) {
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
    if (!ReadRequiredString(
          binding, "shape_virtual_path", out.shape_virtual_path, error)) {
      return false;
    }
    if (!ReadRequiredString(
          binding, "material_virtual_path", out.material_virtual_path, error)) {
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
    if (!ReadRequiredString(
          binding, "shape_virtual_path", out.shape_virtual_path, error)) {
      return false;
    }

    out.record.node_index = node_index;
    return ReadOptionalFloat(binding, "mass", out.record.mass, error)
      && ReadOptionalFloat(
        binding, "max_slope_angle", out.record.max_slope_angle, error)
      && ReadOptionalFloat(
        binding, "step_height", out.record.step_height, error)
      && ReadOptionalFloat(
        binding, "max_strength", out.record.max_strength, error)
      && ReadOptionalUInt16(
        binding, "collision_layer", out.record.collision_layer, error)
      && ReadOptionalUInt32(
        binding, "collision_mask", out.record.collision_mask, error);
  }

  auto ParseSoftBodyBinding(const nlohmann::json& binding,
    phys::SoftBodyBindingRecord& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)) {
      return false;
    }
    out.node_index = node_index;
    return ReadOptionalUInt32(
             binding, "cluster_count", out.cluster_count, error)
      && ReadOptionalFloat(binding, "stiffness", out.stiffness, error)
      && ReadOptionalFloat(binding, "damping", out.damping, error)
      && ReadOptionalFloat(
        binding, "edge_compliance", out.edge_compliance, error)
      && ReadOptionalFloat(
        binding, "shear_compliance", out.shear_compliance, error)
      && ReadOptionalFloat(
        binding, "bend_compliance", out.bend_compliance, error)
      && ParseTetherMode(binding, out.tether_mode, error)
      && ReadOptionalFloat(binding, "tether_max_distance_multiplier",
        out.tether_max_distance_multiplier, error);
  }

  auto ParseJointBinding(const nlohmann::json& binding,
    phys::JointBindingRecord& out, std::string& error) -> bool
  {
    uint32_t node_index_a = 0;
    uint32_t node_index_b = 0;
    uint32_t constraint_resource_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index_a", node_index_a, error)
      || !ReadRequiredUInt32(binding, "node_index_b", node_index_b, error)
      || !ReadRequiredUInt32(binding, "constraint_resource_index",
        constraint_resource_index, error)) {
      return false;
    }
    out.node_index_a = node_index_a;
    out.node_index_b = node_index_b;
    out.constraint_resource_index
      = data::pak::core::ResourceIndexT { constraint_resource_index };
    return true;
  }

  auto ParseVehicleBinding(const nlohmann::json& binding,
    phys::VehicleBindingRecord& out, std::string& error) -> bool
  {
    uint32_t node_index = 0;
    uint32_t constraint_resource_index = 0;
    if (!ReadRequiredUInt32(binding, "node_index", node_index, error)
      || !ReadRequiredUInt32(binding, "constraint_resource_index",
        constraint_resource_index, error)) {
      return false;
    }
    out.node_index = node_index;
    out.constraint_resource_index
      = data::pak::core::ResourceIndexT { constraint_resource_index };
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
        [](const auto& record) { return record.node_index; }, session, request);
      ValidateSingletonBindings(
        parsed.vehicles, "vehicles",
        [](const auto& record) { return record.node_index; }, session, request);
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

  struct IndexedAssetEntry final {
    uint32_t asset_index = 0;
    data::AssetType asset_type = data::AssetType::kUnknown;
  };

  using AssetIndexMap = std::unordered_map<data::AssetKey, IndexedAssetEntry>;

  auto BuildAssetIndexMap(const lc::Inspection& inspection) -> AssetIndexMap
  {
    auto out = AssetIndexMap {};
    const auto assets = inspection.Assets();
    out.reserve(assets.size());
    for (size_t i = 0; i < assets.size(); ++i) {
      out.insert_or_assign(assets[i].key,
        IndexedAssetEntry {
          .asset_index = static_cast<uint32_t>(i),
          .asset_type = static_cast<data::AssetType>(assets[i].asset_type),
        });
    }
    return out;
  }

  struct BindingValidationContext final {
    ImportSession& session;
    const ImportRequest& request;
    content::VirtualPathResolver& resolver;
    std::span<const SidecarCookedInspectionContext> cooked_contexts;
    const AssetIndexMap& target_index_map;
    uint32_t node_count = 0;
  };

  auto ResolveAssetIndexForVirtualPath(BindingValidationContext& ctx,
    std::string_view virtual_path, std::string_view object_path,
    const data::AssetType expected_type, std::string_view unresolved_code,
    std::string_view wrong_type_code) -> std::optional<uint32_t>
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

    const auto target_it = ctx.target_index_map.find(*resolved_key);
    if (target_it == ctx.target_index_map.end()) {
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

    if (target_it->second.asset_type != expected_type) {
      AddDiagnosticAtPath(ctx.session, ctx.request, ImportSeverity::kError,
        std::string(wrong_type_code),
        "Resolved reference has unexpected asset type",
        std::string(object_path));
      return std::nullopt;
    }

    return target_it->second.asset_index;
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

  auto ValidateConstraintResourceIndex(
    const data::pak::core::ResourceIndexT index, ImportSession& session,
    const ImportRequest& request, std::string_view object_path) -> bool
  {
    if (index != data::pak::core::kNoResourceIndex) {
      return true;
    }
    AddDiagnosticAtPath(session, request, ImportSeverity::kError,
      "physics.sidecar.constraint_resource_index_invalid",
      "constraint_resource_index must not be zero", std::string(object_path));
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

      const auto shape_index
        = ResolveAssetIndexForVirtualPath(ctx, binding.shape_virtual_path,
          base_path + ".shape_virtual_path", data::AssetType::kCollisionShape,
          "physics.sidecar.shape_ref_unresolved",
          "physics.sidecar.shape_ref_not_collision_shape");
      const auto material_index = ResolveAssetIndexForVirtualPath(ctx,
        binding.material_virtual_path, base_path + ".material_virtual_path",
        data::AssetType::kPhysicsMaterial,
        "physics.sidecar.material_ref_unresolved",
        "physics.sidecar.material_ref_not_physics_material");
      if (shape_index.has_value()) {
        binding.record.shape_asset_index = data::pak::core::ResourceIndexT {
          *shape_index,
        };
      }
      if (material_index.has_value()) {
        binding.record.material_asset_index = data::pak::core::ResourceIndexT {
          *material_index,
        };
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

      const auto shape_index
        = ResolveAssetIndexForVirtualPath(ctx, binding.shape_virtual_path,
          base_path + ".shape_virtual_path", data::AssetType::kCollisionShape,
          "physics.sidecar.shape_ref_unresolved",
          "physics.sidecar.shape_ref_not_collision_shape");
      if (shape_index.has_value()) {
        binding.record.shape_asset_index = data::pak::core::ResourceIndexT {
          *shape_index,
        };
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

  auto ValidateJointBindings(
    const std::vector<phys::JointBindingRecord>& records,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.joints[") + std::to_string(i) + "]";
      const bool node_a_ok = ValidateNodeIndex(records[i].node_index_a,
        ctx.node_count, ctx.session, ctx.request, base_path + ".node_index_a");
      const bool node_b_ok = ValidateNodeIndex(records[i].node_index_b,
        ctx.node_count, ctx.session, ctx.request, base_path + ".node_index_b");
      if (node_a_ok && node_b_ok) {
        (void)ValidateConstraintResourceIndex(
          records[i].constraint_resource_index, ctx.session, ctx.request,
          base_path + ".constraint_resource_index");
      }
    }
  }

  auto ValidateVehicleBindings(
    const std::vector<phys::VehicleBindingRecord>& records,
    BindingValidationContext& ctx) -> void
  {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto base_path
        = std::string("bindings.vehicles[") + std::to_string(i) + "]";
      if (ValidateNodeIndex(records[i].node_index, ctx.node_count, ctx.session,
            ctx.request, base_path + ".node_index")) {
        (void)ValidateConstraintResourceIndex(
          records[i].constraint_resource_index, ctx.session, ctx.request,
          base_path + ".constraint_resource_index");
      }
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
      if ((table.bytes.size() % table.entry_size) != 0U) {
        error = "Physics table payload is not entry-size aligned";
        return std::nullopt;
      }
      const auto record_count = static_cast<uint64_t>(table.bytes.size())
        / static_cast<uint64_t>(table.entry_size);
      if (record_count > (std::numeric_limits<uint32_t>::max)()) {
        error = "Physics table record count overflow";
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
          .count = static_cast<uint32_t>(record_count),
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
  if (!request.physics.has_value()) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.request.invalid_import_kind",
      "Physics sidecar import requires ImportRequest::physics payload");
    co_return false;
  }
  if (request.physics->target_scene_virtual_path.empty()) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.request.target_scene_virtual_path_missing",
      "Physics sidecar import requires target_scene_virtual_path");
    co_return false;
  }
  if (!IsCanonicalVirtualPath(request.physics->target_scene_virtual_path)) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.sidecar.target_scene_virtual_path_invalid",
      "Target scene virtual path must be canonical");
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
  cooked_contexts.reserve(1U + request.cooked_context_roots.size());

  auto primary_context = SidecarCookedInspectionContext {};
  if (!detail::LoadCookedInspectionContext(session->CookedRoot(), *session,
        request, kPhysicsSidecarResolverDiagnostics, primary_context)) {
    co_return false;
  }
  cooked_contexts.push_back(std::move(primary_context));

  for (const auto& context_root : request.cooked_context_roots) {
    auto context = SidecarCookedInspectionContext {};
    if (!detail::LoadCookedInspectionContext(context_root, *session, request,
          kPhysicsSidecarResolverDiagnostics, context)) {
      co_return false;
    }
    cooked_contexts.push_back(std::move(context));
  }

  auto resolver = content::VirtualPathResolver {};
  for (const auto& context : cooked_contexts) {
    try {
      resolver.AddLooseCookedRoot(context.cooked_root);
    } catch (const std::exception& ex) {
      AddDiagnostic(*session, request, ImportSeverity::kError,
        "physics.sidecar.resolver_mount_failed",
        "Failed mounting cooked root for sidecar resolution: "
          + context.cooked_root.string() + " (" + ex.what() + ")");
      co_return false;
    }
  }

  auto resolved_scene_state
    = co_await detail::ResolveTargetSceneState(*session, request, resolver,
      cooked_contexts, *reader, request.physics->target_scene_virtual_path,
      kPhysicsSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  const auto* target_context = detail::ResolveSceneInspectionContextByKey(
    cooked_contexts, resolved_scene_state->scene_key);
  if (target_context == nullptr) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.sidecar.target_scene_missing",
      "Resolved target scene key is not present in cooked scene context");
    co_return false;
  }
  const auto target_index_map = BuildAssetIndexMap(target_context->inspection);
  auto validation_ctx = BindingValidationContext { *session, request, resolver,
    cooked_contexts, target_index_map, resolved_scene_state->node_count };

  ResolveShapeAndMaterialBindings(
    parsed->rigid_bodies, "rigid_bodies", validation_ctx);
  ResolveShapeAndMaterialBindings(
    parsed->colliders, "colliders", validation_ctx);
  ResolveShapeOnlyBindings(parsed->characters, "characters", validation_ctx);
  ValidateNodeBindings(
    parsed->soft_bodies, "soft_bodies",
    [](const auto& record) { return record.node_index; }, validation_ctx);
  ValidateJointBindings(parsed->joints, validation_ctx);
  ValidateVehicleBindings(parsed->vehicles, validation_ctx);
  ValidateNodeBindings(
    parsed->aggregates, "aggregates",
    [](const auto& record) { return record.node_index; }, validation_ctx);

  if (session->HasErrors()) {
    co_return false;
  }

  auto rigid_records = ExtractRecordVector(
    parsed->rigid_bodies, &RigidBodyBindingSource::record);
  auto collider_records
    = ExtractRecordVector(parsed->colliders, &ColliderBindingSource::record);
  auto character_records
    = ExtractRecordVector(parsed->characters, &CharacterBindingSource::record);

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
  SortAndAppendTable(tables, phys::PhysicsBindingType::kSoftBody,
    parsed->soft_bodies, [](const auto& lhs, const auto& rhs) {
      return lhs.node_index < rhs.node_index;
    });
  SortAndAppendTable(tables, phys::PhysicsBindingType::kJoint, parsed->joints,
    [](const auto& lhs, const auto& rhs) {
      if (lhs.node_index_a != rhs.node_index_a) {
        return lhs.node_index_a < rhs.node_index_a;
      }
      return lhs.node_index_b < rhs.node_index_b;
    });
  SortAndAppendTable(tables, phys::PhysicsBindingType::kVehicle,
    parsed->vehicles, [](const auto& lhs, const auto& rhs) {
      return lhs.node_index < rhs.node_index;
    });
  SortAndAppendTable(tables, phys::PhysicsBindingType::kAggregate,
    parsed->aggregates, [](const auto& lhs, const auto& rhs) {
      return lhs.node_index < rhs.node_index;
    });

  const auto sidecar_relpath = ReplaceSceneExtensionWithPhysics(
    resolved_scene_state->scene_descriptor_relpath);
  const auto sidecar_virtual_path = ReplaceSceneExtensionWithPhysics(
    resolved_scene_state->scene_virtual_path);
  auto sidecar_name = std::filesystem::path(sidecar_relpath).stem().string();
  if (sidecar_name.empty()) {
    sidecar_name = "PhysicsScene";
  }

  auto serialize_error = std::string {};
  const auto descriptor_bytes
    = SerializePhysicsSceneAsset(*resolved_scene_state, sidecar_name, tables,
      EffectiveContentHashingEnabled(request.options.with_content_hashing),
      serialize_error);
  if (!descriptor_bytes.has_value()) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.sidecar.descriptor_serialize_failed",
      std::move(serialize_error));
    co_return false;
  }

  const auto sidecar_key
    = util::MakeDeterministicAssetKey(sidecar_virtual_path);
  try {
    session->AssetEmitter().Emit(sidecar_key, data::AssetType::kPhysicsScene,
      sidecar_virtual_path, sidecar_relpath,
      std::span<const std::byte>(*descriptor_bytes));
  } catch (const std::exception& ex) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "physics.sidecar.descriptor_emit_failed",
      "Failed to emit physics sidecar descriptor: " + std::string(ex.what()));
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
