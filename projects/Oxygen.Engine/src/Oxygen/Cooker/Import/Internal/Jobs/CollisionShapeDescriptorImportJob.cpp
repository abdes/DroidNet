//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/CollisionShapeDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;
  namespace phys = data::pak::physics;

  struct DescriptorTarget final {
    std::string relpath;
    std::string virtual_path;
  };

  using AssetTypeMap = std::unordered_map<data::AssetKey, data::AssetType>;

  struct MountedInspectionContext final {
    std::filesystem::path cooked_root;
    lc::Inspection inspection;
    bool is_primary = false;
  };

  struct PayloadResolution final {
    data::AssetKey payload_asset_key = {};
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
    std::string object_path = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  auto GetCollisionShapeDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kCollisionShapeDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "physics.shape.schema_validation_failed",
      .validation_failed_prefix
      = "Collision shape descriptor validation failed: ",
      .validation_overflow_prefix
      = "Collision shape descriptor validation emitted ",
      .validator_failure_code = "physics.shape.schema_validator_failure",
      .validator_failure_prefix
      = "Collision shape descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetCollisionShapeDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, object_path);
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

  [[nodiscard]] auto NormalizeRelPath(std::string relpath) -> std::string
  {
    return std::filesystem::path(std::move(relpath))
      .lexically_normal()
      .generic_string();
  }

  [[nodiscard]] auto ResolveDescriptorTarget(const ImportRequest& request,
    const json& descriptor_doc, const std::filesystem::path& source_path,
    ImportSession& session) -> std::optional<DescriptorTarget>
  {
    auto target = DescriptorTarget {};

    if (descriptor_doc.contains("virtual_path")) {
      target.virtual_path
        = descriptor_doc.at("virtual_path").get<std::string>();
      if (!internal::IsCanonicalVirtualPath(target.virtual_path)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.virtual_path_invalid",
          "virtual_path must be canonical", "virtual_path");
        return std::nullopt;
      }
      if (!internal::TryVirtualPathToRelPath(
            request, target.virtual_path, target.relpath)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.virtual_path_unmounted",
          "virtual_path is outside mounted cooked roots", "virtual_path");
        return std::nullopt;
      }
      target.relpath = NormalizeRelPath(std::move(target.relpath));
      return target;
    }

    auto name = std::string {};
    if (request.job_name.has_value() && !request.job_name->empty()) {
      name = *request.job_name;
    } else if (descriptor_doc.contains("name")) {
      name = descriptor_doc.at("name").get<std::string>();
    } else {
      name = source_path.stem().string();
    }
    if (name.empty()) {
      name = "collision_shape";
    }

    target.relpath
      = request.loose_cooked_layout.CollisionShapeDescriptorRelPath(name);
    target.virtual_path
      = request.loose_cooked_layout.CollisionShapeVirtualPath(name);
    return target;
  }

  [[nodiscard]] auto ResolveAssetKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

  auto BuildProjectedAssetTypeMap(const lc::Inspection& inspection,
    const std::optional<std::pair<data::AssetKey, data::AssetType>>& pending)
    -> AssetTypeMap
  {
    auto out = AssetTypeMap {};
    const auto assets = inspection.Assets();
    out.reserve(assets.size() + (pending.has_value() ? 1U : 0U));
    for (const auto& asset : assets) {
      out.insert_or_assign(
        asset.key, static_cast<data::AssetType>(asset.asset_type));
    }
    if (pending.has_value()) {
      out.insert_or_assign(pending->first, pending->second);
    }
    return out;
  }

  auto LoadMountedInspectionContexts(ImportSession& session,
    const ImportRequest& request, content::VirtualPathResolver& resolver,
    std::vector<MountedInspectionContext>& contexts) -> bool
  {
    contexts.clear();
    auto roots = internal::BuildUniqueMountedCookedRoots(request);
    contexts.reserve(roots.size());

    const auto primary_root = session.CookedRoot().lexically_normal();
    const auto primary_root_key = primary_root.generic_string();
    auto has_primary = false;

    for (auto& root : roots) {
      auto context = MountedInspectionContext {
        .cooked_root = root.lexically_normal(),
        .inspection = {},
        .is_primary = false,
      };
      context.is_primary
        = context.cooked_root.generic_string() == primary_root_key;

      auto load_error = std::optional<std::string> {};
      try {
        context.inspection.LoadFromRoot(context.cooked_root);
      } catch (const std::exception& ex) {
        load_error = ex.what();
      }
      if (load_error.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.index_load_failed",
          "Failed loading cooked index: " + *load_error,
          context.cooked_root.generic_string());
        return false;
      }

      auto mount_error = std::optional<std::string> {};
      try {
        resolver.AddLooseCookedRoot(context.cooked_root);
      } catch (const std::exception& ex) {
        mount_error = ex.what();
      }
      if (mount_error.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.resolver_mount_failed",
          "Failed mounting cooked root for shape resolution: "
            + context.cooked_root.generic_string() + " (" + *mount_error + ")");
        return false;
      }

      if (context.is_primary) {
        has_primary = true;
      }
      contexts.push_back(std::move(context));
    }

    if (!has_primary) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.primary_source_missing",
        "Primary cooked root is not present in mounted inspection contexts");
      return false;
    }
    return true;
  }

  [[nodiscard]] auto FindPrimaryInspection(
    const std::vector<MountedInspectionContext>& contexts)
    -> const lc::Inspection*
  {
    for (const auto& context : contexts) {
      if (context.is_primary) {
        return &context.inspection;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto KeyExistsInNonPrimaryContexts(
    const std::vector<MountedInspectionContext>& contexts,
    const data::AssetKey& key) -> bool
  {
    for (const auto& context : contexts) {
      if (context.is_primary) {
        continue;
      }
      for (const auto& asset : context.inspection.Assets()) {
        if (asset.key == key) {
          return true;
        }
      }
    }
    return false;
  }

  auto ResolvePhysicsMaterialKey(ImportSession& session,
    const ImportRequest& request, content::VirtualPathResolver& resolver,
    const std::vector<MountedInspectionContext>& contexts,
    const AssetTypeMap& primary_assets, const std::string_view material_ref,
    data::AssetKey& out_material_key) -> bool
  {
    auto resolved_key = std::optional<data::AssetKey> {};
    auto resolve_error = std::optional<std::string> {};
    try {
      resolved_key = resolver.ResolveAssetKey(material_ref);
    } catch (const std::exception& ex) {
      resolve_error = ex.what();
    }
    if (resolve_error.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.material_ref_invalid",
        "Invalid material_ref virtual path: " + *resolve_error, "material_ref");
      return false;
    }
    if (!resolved_key.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.material_ref_unresolved",
        "Resolved key not found for material_ref: " + std::string(material_ref),
        "material_ref");
      return false;
    }

    const auto primary_it = primary_assets.find(*resolved_key);
    if (primary_it == primary_assets.end()) {
      const auto found_elsewhere
        = KeyExistsInNonPrimaryContexts(contexts, *resolved_key);
      AddDiagnostic(session, request, ImportSeverity::kError,
        found_elsewhere ? "physics.shape.reference_source_mismatch"
                        : "physics.shape.material_ref_unresolved",
        found_elsewhere
          ? "Resolved material_ref is not in the target source domain"
          : "Resolved key not found for material_ref: "
            + std::string(material_ref),
        "material_ref");
      return false;
    }

    if (primary_it->second != data::AssetType::kPhysicsMaterial) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.material_ref_not_physics_material",
        "Resolved material_ref has unexpected asset type", "material_ref");
      return false;
    }

    out_material_key = *resolved_key;
    return true;
  }

#pragma pack(push, 1)
  struct AuthoredShapeBlobHeader final {
    char magic[4] = { 'O', 'P', 'S', 'B' };
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
    if (payload.size() > (std::numeric_limits<uint32_t>::max)()) {
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
      std::memcpy(
        out_bytes.data() + sizeof(header), payload.data(), payload.size());
    }
    return true;
  }

  auto EmitAuthoredShapePayload(ImportSession& session,
    const ImportRequest& request, const std::string_view source_id,
    const json& descriptor_doc, const phys::ShapeType shape_type,
    const phys::ShapePayloadType payload_type)
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
    cooked.data.assign(blob_bytes.begin(), blob_bytes.end());
    cooked.format = phys::PhysicsResourceFormat::kJoltShapeBinary;
    cooked.alignment = 16;
    if (EffectiveContentHashingEnabled(request.options.with_content_hashing)) {
      cooked.content_hash = util::ComputeContentSha256(cooked.data);
    }

    try {
      const auto emitted
        = session.PhysicsResourceEmitter().Emit(std::move(cooked), source_id);
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

  auto CopyVec3(const json& doc, const std::string_view field, float (&out)[3])
    -> void
  {
    if (!doc.contains(field)) {
      return;
    }
    const auto values = doc.at(field).get<std::array<float, 3>>();
    out[0] = values[0];
    out[1] = values[1];
    out[2] = values[2];
  }

  auto CopyQuat(const json& doc, const std::string_view field, float (&out)[4])
    -> void
  {
    if (!doc.contains(field)) {
      return;
    }
    const auto values = doc.at(field).get<std::array<float, 4>>();
    out[0] = values[0];
    out[1] = values[1];
    out[2] = values[2];
    out[3] = values[3];
  }

  auto ApplyShapeSpecificParameters(ImportSession& session,
    const ImportRequest& request, const json& descriptor_doc,
    const phys::ShapeType shape_type, phys::CollisionShapeAssetDesc& descriptor)
    -> bool
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
      descriptor.shape_params.box.half_extents[0] = half_extents[0];
      descriptor.shape_params.box.half_extents[1] = half_extents[1];
      descriptor.shape_params.box.half_extents[2] = half_extents[2];
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
      descriptor.shape_params.plane.normal[0] = normal[0];
      descriptor.shape_params.plane.normal[1] = normal[1];
      descriptor.shape_params.plane.normal[2] = normal[2];
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
      for (size_t i = 0; i < limits_min.size(); ++i) {
        descriptor.shape_params.world_boundary.limits_min[i] = limits_min[i];
        descriptor.shape_params.world_boundary.limits_max[i] = limits_max[i];
      }
      if (limits_min[0] > limits_max[0] || limits_min[1] > limits_max[1]
        || limits_min[2] > limits_max[2]) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.world_boundary_limits_invalid",
          "limits_min must be <= limits_max for each axis", "limits_min");
        return false;
      }
      break;
    }
    case phys::ShapeType::kConvexHull:
    case phys::ShapeType::kTriangleMesh:
    case phys::ShapeType::kHeightField:
    case phys::ShapeType::kCompound:
    case phys::ShapeType::kInvalid:
      break;
    }

    return true;
  }

  auto ParseCompoundChild(const json& child_doc, const std::string& child_path,
    ImportSession& session, const ImportRequest& request,
    phys::CompoundShapeChildDesc& out_child) -> bool
  {
    auto child_error = std::optional<std::string> {};
    auto child_type = phys::ShapeType::kInvalid;
    try {
      child_type
        = ParseShapeType(child_doc.at("shape_type").get<std::string>());
      out_child.shape_type = static_cast<uint32_t>(child_type);
      out_child.local_position[0]
        = child_doc.at("local_position").at(0).get<float>();
      out_child.local_position[1]
        = child_doc.at("local_position").at(1).get<float>();
      out_child.local_position[2]
        = child_doc.at("local_position").at(2).get<float>();
      out_child.local_rotation[0]
        = child_doc.at("local_rotation").at(0).get<float>();
      out_child.local_rotation[1]
        = child_doc.at("local_rotation").at(1).get<float>();
      out_child.local_rotation[2]
        = child_doc.at("local_rotation").at(2).get<float>();
      out_child.local_rotation[3]
        = child_doc.at("local_rotation").at(3).get<float>();
      out_child.local_scale[0] = child_doc.at("local_scale").at(0).get<float>();
      out_child.local_scale[1] = child_doc.at("local_scale").at(1).get<float>();
      out_child.local_scale[2] = child_doc.at("local_scale").at(2).get<float>();
    } catch (const std::exception& ex) {
      child_error = ex.what();
    }
    if (child_error.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.schema_contract_mismatch",
        "Compound child extraction failed after schema validation: "
          + *child_error,
        child_path);
      return false;
    }

    if (child_type == phys::ShapeType::kCompound) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.compound_child_invalid",
        "Nested compound child shapes are not supported", child_path);
      return false;
    }

    try {
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
      case phys::ShapeType::kBox:
        out_child.half_extents[0]
          = child_doc.at("half_extents").at(0).get<float>();
        out_child.half_extents[1]
          = child_doc.at("half_extents").at(1).get<float>();
        out_child.half_extents[2]
          = child_doc.at("half_extents").at(2).get<float>();
        break;
      case phys::ShapeType::kPlane:
        out_child.normal[0] = child_doc.at("normal").at(0).get<float>();
        out_child.normal[1] = child_doc.at("normal").at(1).get<float>();
        out_child.normal[2] = child_doc.at("normal").at(2).get<float>();
        out_child.distance = child_doc.at("distance").get<float>();
        break;
      case phys::ShapeType::kWorldBoundary: {
        out_child.boundary_mode = ParseWorldBoundaryMode(
          child_doc.at("boundary_mode").get<std::string>());
        out_child.limits_min[0] = child_doc.at("limits_min").at(0).get<float>();
        out_child.limits_min[1] = child_doc.at("limits_min").at(1).get<float>();
        out_child.limits_min[2] = child_doc.at("limits_min").at(2).get<float>();
        out_child.limits_max[0] = child_doc.at("limits_max").at(0).get<float>();
        out_child.limits_max[1] = child_doc.at("limits_max").at(1).get<float>();
        out_child.limits_max[2] = child_doc.at("limits_max").at(2).get<float>();
        if (out_child.limits_min[0] > out_child.limits_max[0]
          || out_child.limits_min[1] > out_child.limits_max[1]
          || out_child.limits_min[2] > out_child.limits_max[2]) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "physics.shape.world_boundary_limits_invalid",
            "Compound child limits_min must be <= limits_max for each axis",
            child_path + ".limits_min");
          return false;
        }
        break;
      }
      case phys::ShapeType::kInvalid:
      case phys::ShapeType::kConvexHull:
      case phys::ShapeType::kTriangleMesh:
      case phys::ShapeType::kHeightField:
      case phys::ShapeType::kCompound:
        AddDiagnostic(session, request, ImportSeverity::kError,
          "physics.shape.compound_child_invalid",
          "Compound children must use analytic shape types only", child_path);
        return false;
      }
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "physics.shape.schema_contract_mismatch",
        "Compound child parameter extraction failed after schema validation: "
          + std::string(ex.what()),
        child_path);
      return false;
    }

    return true;
  }

  auto ParseCompoundChildren(const json& descriptor_doc, ImportSession& session,
    const ImportRequest& request,
    std::vector<phys::CompoundShapeChildDesc>& out_children) -> bool
  {
    out_children.clear();
    if (!descriptor_doc.contains("children")) {
      return true;
    }

    const auto& children = descriptor_doc.at("children");
    out_children.reserve(children.size());
    for (size_t i = 0; i < children.size(); ++i) {
      const auto child_path = "children[" + std::to_string(i) + "]";
      auto child_desc = phys::CompoundShapeChildDesc {};
      if (!ParseCompoundChild(
            children[i], child_path, session, request, child_desc)) {
        continue;
      }
      out_children.push_back(child_desc);
    }
    return !session.HasErrors();
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
  auto session = ImportSession(Request(), FileReader(), FileWriter(),
    ThreadPool(), TableRegistry(), IndexRegistry());

  if (!Request().collision_shape_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.request_invalid",
      "CollisionShapeDescriptorImportJob requires request "
      "collision_shape_descriptor payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid collision shape descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = json::parse(
      Request().collision_shape_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }
  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid collision shape payload");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid collision shape payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto resolver = content::VirtualPathResolver {};
  auto contexts = std::vector<MountedInspectionContext> {};
  if (!LoadMountedInspectionContexts(session, Request(), resolver, contexts)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor context loading failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto target = ResolveDescriptorTarget(
    Request(), descriptor_doc, Request().source_path, session);
  if (!target.has_value()) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor target resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto primary_inspection = FindPrimaryInspection(contexts);
  if (primary_inspection == nullptr) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.primary_source_missing",
      "Primary source asset index is not available");
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor context loading failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto shape_key = ResolveAssetKey(Request(), target->virtual_path);
  const auto primary_assets = BuildProjectedAssetTypeMap(*primary_inspection,
    std::pair<data::AssetKey, data::AssetType> {
      shape_key,
      data::AssetType::kCollisionShape,
    });

  auto descriptor = phys::CollisionShapeAssetDesc {};
  auto shape_type = phys::ShapeType::kInvalid;
  auto descriptor_error = std::optional<std::string> {};
  try {
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kCollisionShape);
    descriptor.header.version = phys::kCollisionShapeAssetVersion;
    const auto shape_name
      = std::filesystem::path(target->relpath).stem().string();
    util::TruncateAndNullTerminate(
      descriptor.header.name, sizeof(descriptor.header.name), shape_name);

    shape_type
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
  } catch (const std::exception& ex) {
    descriptor_error = ex.what();
  }
  if (descriptor_error.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "physics.shape.schema_contract_mismatch",
      "Schema validated descriptor failed extraction: " + *descriptor_error);
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor payload extraction failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ApplyShapeSpecificParameters(
        session, Request(), descriptor_doc, shape_type, descriptor)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape descriptor parameter validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto compound_children = std::vector<phys::CompoundShapeChildDesc> {};
  if (shape_type == phys::ShapeType::kCompound
    && !ParseCompoundChildren(
      descriptor_doc, session, Request(), compound_children)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape compound child parsing failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto material_key = data::AssetKey {};
  const auto material_ref
    = descriptor_doc.at("material_ref").get<std::string>();
  if (!ResolvePhysicsMaterialKey(session, Request(), resolver, contexts,
        primary_assets, material_ref, material_key)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Collision shape material_ref resolution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  descriptor.material_asset_key = material_key;

  if (const auto payload_type = PayloadTypeForShapeType(shape_type);
    payload_type.has_value()) {
    const auto payload = EmitAuthoredShapePayload(session, Request(),
      target->virtual_path, descriptor_doc, shape_type, *payload_type);
    if (!payload.has_value()) {
      ReportPhaseProgress(
        ImportPhase::kFailed, 1.0F, "Collision shape payload emission failed");
      co_return co_await FinalizeWithTelemetry(session);
    }

    descriptor.cooked_shape_ref.payload_asset_key = payload->payload_asset_key;
    descriptor.cooked_shape_ref.payload_type = payload->payload_type;
  }

  auto pipeline = CollisionShapeImportPipeline(*ThreadPool(),
    CollisionShapeImportPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

  auto item = CollisionShapeImportPipeline::WorkItem {
    .source_id = target->virtual_path,
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
    target->virtual_path, target->relpath, result.descriptor_bytes);
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
