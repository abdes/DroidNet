//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;
  namespace lc = oxygen::content::lc;

  struct MountedInspection final {
    std::filesystem::path root;
    std::optional<lc::Inspection> inspection;
  };

  struct SceneDescriptorExecutionContext final {
    ImportSession& session;
    const ImportRequest& request;
    std::vector<MountedInspection> mounts;
    std::unordered_map<std::string, std::pair<data::AssetKey, data::AssetType>>
      index_cache;
  };

  struct PreparedSceneDescriptor final {
    std::string scene_name;
    SceneBuild build;
    std::vector<data::AssetKey> geometry_keys;
  };

  class SceneDescriptorAdapter final {
  public:
    explicit SceneDescriptorAdapter(SceneBuild build)
      : build_(std::move(build))
    {
    }

    [[nodiscard]] auto BuildSceneStage(const SceneStageInput& stage_input,
      std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
    {
      if (stage_input.stop_token.stop_requested()) {
        diagnostics.push_back({
          .severity = ImportSeverity::kError,
          .code = "import.canceled",
          .message = "Import canceled",
          .source_path = std::string(stage_input.source_id),
          .object_path = {},
        });
        return {};
      }

      return SceneStageResult {
        .build = build_,
        .success = true,
      };
    }

  private:
    SceneBuild build_;
  };

  class StringTableBuilder final {
  public:
    StringTableBuilder()
      : bytes_ { std::byte { 0 } }
    {
      offsets_.insert_or_assign(
        std::string {}, data::pak::core::StringTableOffsetT { 0 });
    }

    [[nodiscard]] auto Add(std::string_view value)
      -> data::pak::core::StringTableOffsetT
    {
      if (value.empty()) {
        return data::pak::core::StringTableOffsetT { 0 };
      }

      if (const auto it = offsets_.find(std::string(value));
        it != offsets_.end()) {
        return it->second;
      }

      const auto offset
        = static_cast<data::pak::core::StringTableOffsetT>(bytes_.size());
      bytes_.reserve(bytes_.size() + value.size() + 1U);
      for (const auto ch : value) {
        bytes_.push_back(static_cast<std::byte>(ch));
      }
      bytes_.push_back(std::byte { 0 });
      offsets_.insert_or_assign(std::string(value), offset);
      return offset;
    }

    [[nodiscard]] auto Build() && -> std::vector<std::byte>
    {
      return std::move(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
    std::unordered_map<std::string, data::pak::core::StringTableOffsetT>
      offsets_;
  };

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

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
  }

  auto ToLowerAscii(std::string value) -> std::string
  {
    for (auto& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  auto GetSceneDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(nlohmann::json::parse(kSceneDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "scene.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Scene descriptor validation failed: ",
      .validation_overflow_prefix = "Scene descriptor validation emitted ",
      .validator_failure_code = "scene.descriptor.schema_validator_failure",
      .validator_failure_prefix = "Scene descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetSceneDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, object_path);
      });
  }

  auto MakeDuration(const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  auto BuildNodeAssetKey(const ImportRequest& request,
    std::string_view scene_virtual_path, const uint32_t node_index)
    -> data::AssetKey
  {
    if (request.options.asset_key_policy == AssetKeyPolicy::kRandom) {
      return util::MakeRandomAssetKey();
    }

    const auto key_path = std::string(scene_virtual_path) + "/nodes/"
      + std::to_string(node_index);
    return util::MakeDeterministicAssetKey(key_path);
  }

  auto InferAssetTypeFromRelPath(const std::string_view relpath)
    -> std::optional<data::AssetType>
  {
    const auto ext
      = ToLowerAscii(std::filesystem::path(relpath).extension().string());
    if (ext == ".ogeo") {
      return data::AssetType::kGeometry;
    }
    if (ext == ".omat") {
      return data::AssetType::kMaterial;
    }
    if (ext == ".oscript") {
      return data::AssetType::kScript;
    }
    if (ext == ".oiact") {
      return data::AssetType::kInputAction;
    }
    if (ext == ".oimap") {
      return data::AssetType::kInputMappingContext;
    }
    if (ext == ".physics") {
      return data::AssetType::kPhysicsScene;
    }
    if (ext == ".oscene") {
      return data::AssetType::kScene;
    }
    return std::nullopt;
  }

  auto LoadMountedInspections(SceneDescriptorExecutionContext& context) -> void
  {
    for (const auto& root :
      internal::BuildUniqueMountedCookedRoots(context.request)) {
      auto mount = MountedInspection {
        .root = root,
        .inspection = std::nullopt,
      };

      const auto index_path = mount.root / "container.index.bin";
      std::error_code ec;
      if (std::filesystem::exists(index_path, ec)) {
        try {
          auto inspection = lc::Inspection {};
          inspection.LoadFromRoot(mount.root);
          mount.inspection = std::move(inspection);
        } catch (const std::exception& ex) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "scene.descriptor.index_load_failed",
            "Failed loading cooked index: " + std::string(ex.what()),
            mount.root.string());
        }
      }

      context.mounts.push_back(std::move(mount));
    }
  }

  auto ResolveAssetReference(SceneDescriptorExecutionContext& context,
    std::string_view virtual_path, std::optional<data::AssetType> expected_type,
    bool require_asset_key, std::string object_path)
    -> std::optional<std::pair<data::AssetKey, data::AssetType>>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_invalid",
        "Reference virtual_path must be canonical", std::move(object_path));
      return std::nullopt;
    }

    if (const auto it = context.index_cache.find(std::string(virtual_path));
      it != context.index_cache.end()) {
      const auto type = it->second.second;
      if (expected_type.has_value() && type != *expected_type) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.reference_type_mismatch",
          "Reference type mismatch; expected "
            + std::string(data::to_string(*expected_type)) + " but found "
            + std::string(data::to_string(type)),
          std::move(object_path));
        return std::nullopt;
      }
      return it->second;
    }

    struct IndexedMatch final {
      data::AssetKey key {};
      data::AssetType type = data::AssetType::kUnknown;
    };
    auto indexed_matches = std::vector<IndexedMatch> {};
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (!it->inspection.has_value()) {
        continue;
      }
      for (const auto& asset : it->inspection->Assets()) {
        if (asset.virtual_path != virtual_path) {
          continue;
        }
        indexed_matches.push_back({
          .key = asset.key,
          .type = static_cast<data::AssetType>(asset.asset_type),
        });
      }
    }

    if (indexed_matches.size() > 1U) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_ambiguous",
        "Reference virtual_path resolved to multiple mounted assets: "
          + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    if (!indexed_matches.empty()) {
      const auto& match = indexed_matches.front();
      if (expected_type.has_value() && match.type != *expected_type) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.reference_type_mismatch",
          "Reference type mismatch; expected "
            + std::string(data::to_string(*expected_type)) + " but found "
            + std::string(data::to_string(match.type)),
          std::move(object_path));
        return std::nullopt;
      }
      context.index_cache.insert_or_assign(
        std::string(virtual_path), std::make_pair(match.key, match.type));
      return std::make_pair(match.key, match.type);
    }

    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          context.request, virtual_path, relpath)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_unmounted",
        "Reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    auto file_matches = std::vector<std::filesystem::path> {};
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto candidate = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) {
        file_matches.push_back(candidate);
      }
    }

    if (file_matches.size() > 1U) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_ambiguous",
        "Reference virtual_path resolved to multiple mounted descriptors: "
          + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    if (file_matches.empty()) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_missing",
        "Reference virtual_path was not found: " + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    const auto inferred_type = InferAssetTypeFromRelPath(relpath);
    if (!inferred_type.has_value()) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_unknown",
        "Unable to infer reference asset type from extension: "
          + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    if (expected_type.has_value() && *inferred_type != *expected_type) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_mismatch",
        "Reference type mismatch; expected "
          + std::string(data::to_string(*expected_type)) + " but found "
          + std::string(data::to_string(*inferred_type)),
        std::move(object_path));
      return std::nullopt;
    }

    if (require_asset_key
      && context.request.options.asset_key_policy == AssetKeyPolicy::kRandom) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_index_resolution_required",
        "Reference requires indexed asset metadata when asset_key_policy is "
        "random",
        std::move(object_path));
      return std::nullopt;
    }

    const auto key = util::MakeDeterministicAssetKey(virtual_path);
    const auto resolved = std::make_pair(key, *inferred_type);
    context.index_cache.insert_or_assign(std::string(virtual_path), resolved);
    return resolved;
  }

  auto ValidateNodeIndex(SceneDescriptorExecutionContext& context,
    const uint32_t node_index, const uint32_t node_count, std::string code,
    std::string message, std::string object_path) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      std::move(code), std::move(message), std::move(object_path));
    return false;
  }

  auto ApplyLightCommon(
    const json& common_doc, data::pak::world::LightCommonRecord& common) -> void
  {
    if (common_doc.contains("affects_world")) {
      common.affects_world
        = common_doc.at("affects_world").get<bool>() ? 1U : 0U;
    }
    if (common_doc.contains("color_rgb")) {
      const auto& color = common_doc.at("color_rgb");
      for (size_t i = 0; i < 3U; ++i) {
        common.color_rgb[i] = color.at(i).get<float>();
      }
    }
    if (common_doc.contains("mobility")) {
      common.mobility
        = static_cast<uint8_t>(common_doc.at("mobility").get<uint32_t>());
    }
    if (common_doc.contains("casts_shadows")) {
      common.casts_shadows
        = common_doc.at("casts_shadows").get<bool>() ? 1U : 0U;
    }
    if (common_doc.contains("exposure_compensation_ev")) {
      common.exposure_compensation_ev
        = common_doc.at("exposure_compensation_ev").get<float>();
    }
    if (common_doc.contains("shadow")) {
      const auto& shadow = common_doc.at("shadow");
      if (shadow.contains("bias")) {
        common.shadow.bias = shadow.at("bias").get<float>();
      }
      if (shadow.contains("normal_bias")) {
        common.shadow.normal_bias = shadow.at("normal_bias").get<float>();
      }
      if (shadow.contains("contact_shadows")) {
        common.shadow.contact_shadows
          = shadow.at("contact_shadows").get<bool>() ? 1U : 0U;
      }
      if (shadow.contains("resolution_hint")) {
        common.shadow.resolution_hint
          = static_cast<uint8_t>(shadow.at("resolution_hint").get<uint32_t>());
      }
    }
  }

  auto BuildNodeFlags(const json& node_doc) -> uint32_t
  {
    auto flags = uint32_t { data::pak::world::kSceneNodeFlag_Visible
      | data::pak::world::kSceneNodeFlag_CastsShadows
      | data::pak::world::kSceneNodeFlag_ReceivesShadows };
    if (!node_doc.contains("flags")) {
      return flags;
    }

    const auto& flags_doc = node_doc.at("flags");
    const auto apply_flag = [&](const char* key, const uint32_t mask) {
      if (!flags_doc.contains(key)) {
        return;
      }
      const auto enabled = flags_doc.at(key).get<bool>();
      if (enabled) {
        flags |= mask;
      } else {
        flags &= ~mask;
      }
    };

    apply_flag("visible", data::pak::world::kSceneNodeFlag_Visible);
    apply_flag("static", data::pak::world::kSceneNodeFlag_Static);
    apply_flag("casts_shadows", data::pak::world::kSceneNodeFlag_CastsShadows);
    apply_flag(
      "receives_shadows", data::pak::world::kSceneNodeFlag_ReceivesShadows);
    apply_flag("ray_cast_selectable",
      data::pak::world::kSceneNodeFlag_RayCastingSelectable);
    apply_flag("ignore_parent_transform",
      data::pak::world::kSceneNodeFlag_IgnoreParentTransform);
    return flags;
  }

  auto PrepareSceneDescriptor(SceneDescriptorExecutionContext& context,
    const json& descriptor_doc) -> std::optional<PreparedSceneDescriptor>
  {
    auto prepared = PreparedSceneDescriptor {
      .scene_name = descriptor_doc.at("name").get<std::string>(),
      .build = {},
      .geometry_keys = {},
    };

    const auto scene_virtual_path
      = context.request.loose_cooked_layout.SceneVirtualPath(
        prepared.scene_name);
    const auto& nodes_doc = descriptor_doc.at("nodes");
    const auto node_count = static_cast<uint32_t>(nodes_doc.size());

    auto string_table = StringTableBuilder {};
    prepared.build.nodes.reserve(nodes_doc.size());

    for (size_t node_i = 0; node_i < nodes_doc.size(); ++node_i) {
      const auto& node_doc = nodes_doc.at(node_i);
      const auto node_path
        = std::string { "nodes[" } + std::to_string(node_i) + "]";

      auto node_record = data::pak::world::NodeRecord {};
      node_record.node_id = BuildNodeAssetKey(
        context.request, scene_virtual_path, static_cast<uint32_t>(node_i));
      if (node_doc.contains("name")) {
        node_record.scene_name_offset
          = string_table.Add(node_doc.at("name").get<std::string>());
      }
      node_record.parent_index = static_cast<uint32_t>(node_i);
      if (node_doc.contains("parent")) {
        const auto parent_index = node_doc.at("parent").get<uint32_t>();
        if (parent_index >= node_count) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "scene.descriptor.node_parent_out_of_range",
            "Node parent index is out of range", node_path + ".parent");
          return std::nullopt;
        }
        node_record.parent_index = parent_index;
      }

      node_record.node_flags = BuildNodeFlags(node_doc);

      if (node_doc.contains("transform")) {
        const auto& transform = node_doc.at("transform");
        if (transform.contains("translation")) {
          const auto& translation = transform.at("translation");
          for (size_t i = 0; i < 3U; ++i) {
            node_record.translation[i] = translation.at(i).get<float>();
          }
        }
        if (transform.contains("rotation")) {
          const auto& rotation = transform.at("rotation");
          for (size_t i = 0; i < 4U; ++i) {
            node_record.rotation[i] = rotation.at(i).get<float>();
          }
        }
        if (transform.contains("scale")) {
          const auto& scale = transform.at("scale");
          for (size_t i = 0; i < 3U; ++i) {
            node_record.scale[i] = scale.at(i).get<float>();
          }
        }
      }

      prepared.build.nodes.push_back(node_record);
    }

    prepared.build.strings = std::move(string_table).Build();

    if (descriptor_doc.contains("renderables")) {
      const auto& renderables_doc = descriptor_doc.at("renderables");
      prepared.build.renderables.reserve(renderables_doc.size());
      for (size_t i = 0; i < renderables_doc.size(); ++i) {
        const auto& renderable_doc = renderables_doc.at(i);
        const auto object_path
          = std::string { "renderables[" } + std::to_string(i) + "]";
        const auto node_index = renderable_doc.at("node").get<uint32_t>();
        if (!ValidateNodeIndex(context, node_index, node_count,
              "scene.descriptor.renderable_node_index_out_of_range",
              "Renderable node index is out of range", object_path + ".node")) {
          return std::nullopt;
        }

        const auto geometry_ref
          = renderable_doc.at("geometry_ref").get<std::string>();
        const auto resolved_geometry
          = ResolveAssetReference(context, geometry_ref,
            data::AssetType::kGeometry, true, object_path + ".geometry_ref");
        if (!resolved_geometry.has_value()) {
          return std::nullopt;
        }

        if (renderable_doc.contains("material_ref")) {
          const auto material_ref
            = renderable_doc.at("material_ref").get<std::string>();
          if (!ResolveAssetReference(context, material_ref,
                data::AssetType::kMaterial, false,
                object_path + ".material_ref")
                .has_value()) {
            return std::nullopt;
          }
        }

        auto renderable = data::pak::world::RenderableRecord {};
        renderable.node_index = node_index;
        renderable.geometry_key = resolved_geometry->first;
        renderable.visible = renderable_doc.value("visible", true) ? 1U : 0U;
        prepared.build.renderables.push_back(renderable);
        prepared.geometry_keys.push_back(renderable.geometry_key);
      }
    }

    if (descriptor_doc.contains("cameras")) {
      const auto& cameras_doc = descriptor_doc.at("cameras");
      if (cameras_doc.contains("perspective")) {
        const auto& perspective_doc = cameras_doc.at("perspective");
        prepared.build.perspective_cameras.reserve(perspective_doc.size());
        for (size_t i = 0; i < perspective_doc.size(); ++i) {
          const auto& camera_doc = perspective_doc.at(i);
          const auto object_path
            = std::string { "cameras.perspective[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Perspective camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto camera = data::pak::world::PerspectiveCameraRecord {};
          camera.node_index = node_index;
          if (camera_doc.contains("fov_y")) {
            camera.fov_y = camera_doc.at("fov_y").get<float>();
          }
          if (camera_doc.contains("aspect_ratio")) {
            camera.aspect_ratio = camera_doc.at("aspect_ratio").get<float>();
          }
          if (camera_doc.contains("near_plane")) {
            camera.near_plane = camera_doc.at("near_plane").get<float>();
          }
          if (camera_doc.contains("far_plane")) {
            camera.far_plane = camera_doc.at("far_plane").get<float>();
          }
          prepared.build.perspective_cameras.push_back(camera);
        }
      }

      if (cameras_doc.contains("orthographic")) {
        const auto& orthographic_doc = cameras_doc.at("orthographic");
        prepared.build.orthographic_cameras.reserve(orthographic_doc.size());
        for (size_t i = 0; i < orthographic_doc.size(); ++i) {
          const auto& camera_doc = orthographic_doc.at(i);
          const auto object_path
            = std::string { "cameras.orthographic[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Orthographic camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto camera = data::pak::world::OrthographicCameraRecord {};
          camera.node_index = node_index;
          if (camera_doc.contains("left")) {
            camera.left = camera_doc.at("left").get<float>();
          }
          if (camera_doc.contains("right")) {
            camera.right = camera_doc.at("right").get<float>();
          }
          if (camera_doc.contains("bottom")) {
            camera.bottom = camera_doc.at("bottom").get<float>();
          }
          if (camera_doc.contains("top")) {
            camera.top = camera_doc.at("top").get<float>();
          }
          if (camera_doc.contains("near_plane")) {
            camera.near_plane = camera_doc.at("near_plane").get<float>();
          }
          if (camera_doc.contains("far_plane")) {
            camera.far_plane = camera_doc.at("far_plane").get<float>();
          }
          prepared.build.orthographic_cameras.push_back(camera);
        }
      }
    }

    if (descriptor_doc.contains("lights")) {
      const auto& lights_doc = descriptor_doc.at("lights");
      if (lights_doc.contains("directional")) {
        const auto& directional_doc = lights_doc.at("directional");
        prepared.build.directional_lights.reserve(directional_doc.size());
        for (size_t i = 0; i < directional_doc.size(); ++i) {
          const auto& light_doc = directional_doc.at(i);
          const auto object_path
            = std::string { "lights.directional[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Directional light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::DirectionalLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("angular_size_radians")) {
            light.angular_size_radians
              = light_doc.at("angular_size_radians").get<float>();
          }
          if (light_doc.contains("environment_contribution")) {
            light.environment_contribution
              = light_doc.at("environment_contribution").get<bool>() ? 1U : 0U;
          }
          if (light_doc.contains("is_sun_light")) {
            light.is_sun_light
              = light_doc.at("is_sun_light").get<bool>() ? 1U : 0U;
          }
          if (light_doc.contains("cascade_count")) {
            light.cascade_count = light_doc.at("cascade_count").get<uint32_t>();
          }
          if (light_doc.contains("cascade_distances")) {
            const auto& distances = light_doc.at("cascade_distances");
            for (size_t d = 0; d < 4U; ++d) {
              light.cascade_distances[d] = distances.at(d).get<float>();
            }
          }
          if (light_doc.contains("distribution_exponent")) {
            light.distribution_exponent
              = light_doc.at("distribution_exponent").get<float>();
          }
          if (light_doc.contains("intensity_lux")) {
            light.intensity_lux = light_doc.at("intensity_lux").get<float>();
          }
          prepared.build.directional_lights.push_back(light);
        }
      }

      if (lights_doc.contains("point")) {
        const auto& point_doc = lights_doc.at("point");
        prepared.build.point_lights.reserve(point_doc.size());
        for (size_t i = 0; i < point_doc.size(); ++i) {
          const auto& light_doc = point_doc.at(i);
          const auto object_path
            = std::string { "lights.point[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Point light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::PointLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("range")) {
            light.range = light_doc.at("range").get<float>();
          }
          if (light_doc.contains("attenuation_model")) {
            light.attenuation_model = static_cast<uint8_t>(
              light_doc.at("attenuation_model").get<uint32_t>());
          }
          if (light_doc.contains("decay_exponent")) {
            light.decay_exponent = light_doc.at("decay_exponent").get<float>();
          }
          if (light_doc.contains("source_radius")) {
            light.source_radius = light_doc.at("source_radius").get<float>();
          }
          if (light_doc.contains("luminous_flux_lm")) {
            light.luminous_flux_lm
              = light_doc.at("luminous_flux_lm").get<float>();
          }
          prepared.build.point_lights.push_back(light);
        }
      }

      if (lights_doc.contains("spot")) {
        const auto& spot_doc = lights_doc.at("spot");
        prepared.build.spot_lights.reserve(spot_doc.size());
        for (size_t i = 0; i < spot_doc.size(); ++i) {
          const auto& light_doc = spot_doc.at(i);
          const auto object_path
            = std::string { "lights.spot[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Spot light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::SpotLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("range")) {
            light.range = light_doc.at("range").get<float>();
          }
          if (light_doc.contains("attenuation_model")) {
            light.attenuation_model = static_cast<uint8_t>(
              light_doc.at("attenuation_model").get<uint32_t>());
          }
          if (light_doc.contains("decay_exponent")) {
            light.decay_exponent = light_doc.at("decay_exponent").get<float>();
          }
          if (light_doc.contains("inner_cone_angle_radians")) {
            light.inner_cone_angle_radians
              = light_doc.at("inner_cone_angle_radians").get<float>();
          }
          if (light_doc.contains("outer_cone_angle_radians")) {
            light.outer_cone_angle_radians
              = light_doc.at("outer_cone_angle_radians").get<float>();
          }
          if (light_doc.contains("source_radius")) {
            light.source_radius = light_doc.at("source_radius").get<float>();
          }
          if (light_doc.contains("luminous_flux_lm")) {
            light.luminous_flux_lm
              = light_doc.at("luminous_flux_lm").get<float>();
          }
          prepared.build.spot_lights.push_back(light);
        }
      }
    }

    if (descriptor_doc.contains("references")) {
      const auto& refs_doc = descriptor_doc.at("references");

      const auto validate_ref_array =
        [&](std::string_view key, std::optional<data::AssetType> type) -> bool {
        if (!refs_doc.contains(std::string(key))) {
          return true;
        }
        const auto& values = refs_doc.at(std::string(key));
        for (size_t i = 0; i < values.size(); ++i) {
          const auto object_path = std::string { "references." }
            + std::string(key) + "[" + std::to_string(i) + "]";
          const auto virtual_path = values.at(i).get<std::string>();
          if (!ResolveAssetReference(
                context, virtual_path, type, false, object_path)
                .has_value()) {
            return false;
          }
        }
        return true;
      };

      if (!validate_ref_array("materials", data::AssetType::kMaterial)
        || !validate_ref_array("scripts", data::AssetType::kScript)
        || !validate_ref_array("input_actions", data::AssetType::kInputAction)
        || !validate_ref_array(
          "input_mapping_contexts", data::AssetType::kInputMappingContext)
        || !validate_ref_array(
          "physics_sidecars", data::AssetType::kPhysicsScene)
        || !validate_ref_array("extra_assets", std::nullopt)) {
        return std::nullopt;
      }
    }

    return prepared;
  }

} // namespace

auto SceneDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting scene descriptor job: job_id={} path={}", JobId(),
    Request().source_path.string());

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

  if (!Request().scene_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "scene.descriptor.request_invalid",
      "SceneDescriptorImportJob requires request.scene_descriptor payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid scene descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = nlohmann::json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = nlohmann::json::parse(
      Request().scene_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }

  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "scene.descriptor.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid scene descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "scene.descriptor.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid scene descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = SceneDescriptorExecutionContext {
    .session = session,
    .request = Request(),
    .mounts = {},
    .index_cache = {},
  };
  LoadMountedInspections(context);

  const auto prepared_opt = PrepareSceneDescriptor(context, descriptor_doc);
  if (!prepared_opt.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (session.HasErrors()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto& prepared = *prepared_opt;
  auto request_for_pipeline = Request();
  request_for_pipeline.source_path = std::filesystem::path(prepared.scene_name);
  auto pipeline = ScenePipeline(*ThreadPool(),
    ScenePipeline::Config {
      .queue_capacity = Concurrency().scene.queue_capacity,
      .worker_count = Concurrency().scene.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

  auto adapter = std::make_shared<SceneDescriptorAdapter>(prepared.build);
  auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
    Request().source_path.string(), prepared.geometry_keys,
    std::vector<SceneEnvironmentSystem> {}, std::move(request_for_pipeline),
    observer_ptr { &GetNamingService() }, StopToken());

  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }
  if (result.telemetry.load_duration.has_value()) {
    session.AddLoadDuration(*result.telemetry.load_duration);
  }
  if (result.telemetry.io_duration.has_value()) {
    session.AddIoDuration(*result.telemetry.io_duration);
  }
  AddDiagnostics(session, std::move(result.diagnostics));

  if (!result.success || !result.cooked.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  const auto& cooked = *result.cooked;
  session.AssetEmitter().Emit(cooked.scene_key, data::AssetType::kScene,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes);
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto SceneDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
