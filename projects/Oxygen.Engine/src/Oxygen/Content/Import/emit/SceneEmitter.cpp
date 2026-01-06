//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/SceneEmitter.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/fbx/UfbxUtils.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>
#include <Oxygen/Content/Import/util/StringUtils.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::emit {

namespace {

  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::ComponentType;
  using oxygen::data::pak::DirectionalLightRecord;
  using oxygen::data::pak::NodeRecord;
  using oxygen::data::pak::OrthographicCameraRecord;
  using oxygen::data::pak::PerspectiveCameraRecord;
  using oxygen::data::pak::PointLightRecord;
  using oxygen::data::pak::RenderableRecord;
  using oxygen::data::pak::SceneAssetDesc;
  using oxygen::data::pak::SceneComponentTableDesc;
  using oxygen::data::pak::SceneEnvironmentBlockHeader;
  using oxygen::data::pak::SpotLightRecord;

  [[nodiscard]] auto TryFindRealProp(const ufbx_props& props,
    const char* name) noexcept -> std::optional<ufbx_real>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_REAL) == 0) {
      return std::nullopt;
    }
    return prop->value_real;
  }

  [[nodiscard]] auto TryFindBoolProp(
    const ufbx_props& props, const char* name) noexcept -> std::optional<bool>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_INT) == 0) {
      return std::nullopt;
    }
    return prop->value_int != 0;
  }

  [[nodiscard]] auto TryFindVec3Prop(const ufbx_props& props,
    const char* name) noexcept -> std::optional<ufbx_vec3>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_VEC3) == 0) {
      return std::nullopt;
    }
    return prop->value_vec3;
  }

  [[nodiscard]] auto ToRadiansHeuristic(const ufbx_real angle) noexcept -> float
  {
    const auto a = static_cast<float>(angle);
    constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
    if (a > kTwoPi + 1e-3F) {
      return a * (std::numbers::pi_v<float> / 180.0F);
    }
    return a;
  }

  [[nodiscard]] auto ResolveLightRange(const ufbx_light& light) noexcept
    -> std::optional<float>
  {
    for (const auto* name : {
           "FarAttenuationEnd",
           "DecayStart",
           "Range",
           "Radius",
           "FalloffEnd",
         }) {
      if (const auto v = TryFindRealProp(light.props, name); v.has_value()) {
        const auto fv = util::ToFloat(*v);
        if (std::isfinite(fv) && fv > 0.0F) {
          return fv;
        }
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto ResolveLightSourceRadius(const ufbx_light& light) noexcept
    -> std::optional<float>
  {
    for (const auto* name : {
           "SourceRadius",
           "AreaRadius",
           "Radius",
         }) {
      if (const auto v = TryFindRealProp(light.props, name); v.has_value()) {
        const auto fv = util::ToFloat(*v);
        if (std::isfinite(fv) && fv >= 0.0F) {
          return fv;
        }
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto MapDecayToAttenuation(const ufbx_light_decay decay)
    -> std::pair<std::uint8_t, float>
  {
    switch (decay) {
    case UFBX_LIGHT_DECAY_LINEAR:
      return { 1, 1.0F };
    case UFBX_LIGHT_DECAY_QUADRATIC:
      return { 0, 2.0F };
    case UFBX_LIGHT_DECAY_CUBIC:
      return { 2, 3.0F };
    case UFBX_LIGHT_DECAY_NONE:
    default:
      return { 2, 0.0F };
    }
  }

  auto FillLightCommon(const ufbx_light& light,
    oxygen::data::pak::LightCommonRecord& out) noexcept -> void
  {
    out.affects_world = light.cast_light ? 1U : 0U;
    out.color_rgb[0] = std::max(0.0F, util::ToFloat(light.color.x));
    out.color_rgb[1] = std::max(0.0F, util::ToFloat(light.color.y));
    out.color_rgb[2] = std::max(0.0F, util::ToFloat(light.color.z));
    out.intensity = std::max(0.0F, util::ToFloat(light.intensity));

    out.mobility = 0;
    out.casts_shadows = light.cast_shadows ? 1U : 0U;

    if (const auto v = TryFindBoolProp(light.props, "CastShadows");
      v.has_value()) {
      out.casts_shadows = v.value() ? 1U : 0U;
    }
    if (const auto v = TryFindBoolProp(light.props, "CastLight");
      v.has_value()) {
      out.affects_world = v.value() ? 1U : 0U;
    }

    if (const auto v = TryFindVec3Prop(light.props, "Color"); v.has_value()) {
      out.color_rgb[0] = std::max(0.0F, util::ToFloat(v->x));
      out.color_rgb[1] = std::max(0.0F, util::ToFloat(v->y));
      out.color_rgb[2] = std::max(0.0F, util::ToFloat(v->z));
    }

    if (const auto v = TryFindRealProp(light.props, "ExposureCompensation");
      v.has_value()) {
      out.exposure_compensation_ev = util::ToFloat(*v);
    }
    if (const auto v = TryFindRealProp(light.props, "ShadowBias");
      v.has_value()) {
      out.shadow.bias = util::ToFloat(*v);
    }
    if (const auto v = TryFindRealProp(light.props, "ShadowNormalBias");
      v.has_value()) {
      out.shadow.normal_bias = util::ToFloat(*v);
    }
    if (const auto v = TryFindBoolProp(light.props, "ContactShadows");
      v.has_value()) {
      out.shadow.contact_shadows = v.value() ? 1U : 0U;
    }
  }

  struct SceneBuild final {
    std::vector<NodeRecord> nodes;
    std::vector<std::byte> strings;

    std::vector<RenderableRecord> renderables;
    std::vector<PerspectiveCameraRecord> perspective_cameras;
    std::vector<OrthographicCameraRecord> orthographic_cameras;
    std::vector<DirectionalLightRecord> directional_lights;
    std::vector<PointLightRecord> point_lights;
    std::vector<SpotLightRecord> spot_lights;

    size_t camera_attr_total = 0;
    size_t camera_attr_skipped = 0;
    size_t light_attr_total = 0;
    size_t light_attr_skipped = 0;

    struct NodeRef final {
      const ufbx_node* node = nullptr;
      uint32_t index = 0;
      std::string name;
    };

    std::vector<NodeRef> node_refs;
  };

  [[nodiscard]] auto FindGeometryKey(const ufbx_mesh* mesh,
    const std::vector<ImportedGeometry>& geometry) -> std::optional<AssetKey>
  {
    if (mesh == nullptr) {
      return std::nullopt;
    }

    for (const auto& g : geometry) {
      if (g.mesh == mesh) {
        return g.key;
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] auto AppendString(std::vector<std::byte>& strings,
    const std::string_view s) -> oxygen::data::pak::StringTableOffsetT
  {
    const auto offset
      = static_cast<oxygen::data::pak::StringTableOffsetT>(strings.size());

    const auto bytes = std::as_bytes(std::span<const char>(s.data(), s.size()));
    strings.insert(strings.end(), bytes.begin(), bytes.end());
    strings.push_back(std::byte { 0 });

    return offset;
  }

  [[nodiscard]] auto MakeNodeKey(const std::string_view node_virtual_path)
    -> AssetKey
  {
    return util::MakeDeterministicAssetKey(node_virtual_path);
  }

  auto AddCameraComponents(SceneBuild& build, const ufbx_camera& cam,
    uint32_t node_index, const std::string_view name) -> void
  {
    ++build.camera_attr_total;

    if (cam.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE) {
      float near_plane = std::abs(util::ToFloat(cam.near_plane));
      float far_plane = std::abs(util::ToFloat(cam.far_plane));
      if (far_plane < near_plane) {
        std::swap(far_plane, near_plane);
      }

      const float fov_y_rad = util::ToFloat(cam.field_of_view_deg.y)
        * (std::numbers::pi_v<float> / 180.0f);

      build.perspective_cameras.push_back(PerspectiveCameraRecord {
        .node_index = node_index,
        .fov_y = fov_y_rad,
        .aspect_ratio = util::ToFloat(cam.aspect_ratio),
        .near_plane = near_plane,
        .far_plane = far_plane,
        .reserved = {},
      });
      return;
    }

    if (cam.projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) {
      float near_plane = std::abs(util::ToFloat(cam.near_plane));
      float far_plane = std::abs(util::ToFloat(cam.far_plane));
      if (far_plane < near_plane) {
        std::swap(far_plane, near_plane);
      }

      const float half_w = util::ToFloat(cam.orthographic_size.x) * 0.5f;
      const float half_h = util::ToFloat(cam.orthographic_size.y) * 0.5f;

      build.orthographic_cameras.push_back(OrthographicCameraRecord {
        .node_index = node_index,
        .left = -half_w,
        .right = half_w,
        .bottom = -half_h,
        .top = half_h,
        .near_plane = near_plane,
        .far_plane = far_plane,
        .reserved = {},
      });
      return;
    }

    ++build.camera_attr_skipped;
    LOG_F(INFO,
      "Scene camera attribute skipped: node_index={} name='{}' "
      "projection_mode={}",
      node_index, std::string(name).c_str(),
      static_cast<int>(cam.projection_mode));
  }

  auto AddLightComponents(SceneBuild& build, const ufbx_light& light,
    uint32_t node_index, const ImportRequest& request, CookedContentWriter& out,
    const std::string_view name) -> void
  {
    ++build.light_attr_total;

    const auto [atten_model, decay_exponent]
      = MapDecayToAttenuation(light.decay);

    switch (light.type) {
    case UFBX_LIGHT_DIRECTIONAL: {
      DirectionalLightRecord rec_light {};
      rec_light.node_index = node_index;
      FillLightCommon(light, rec_light.common);

      if (const auto v = TryFindRealProp(light.props, "AngularSize");
        v.has_value()) {
        rec_light.angular_size_radians = ToRadiansHeuristic(*v);
      } else if (const auto v2
        = TryFindRealProp(light.props, "AngularDiameter");
        v2.has_value()) {
        rec_light.angular_size_radians = ToRadiansHeuristic(*v2);
      }

      if (const auto v
        = TryFindBoolProp(light.props, "EnvironmentContribution");
        v.has_value()) {
        rec_light.environment_contribution = v.value() ? 1U : 0U;
      }

      build.directional_lights.push_back(rec_light);
      break;
    }

    case UFBX_LIGHT_POINT:
    case UFBX_LIGHT_AREA:
    case UFBX_LIGHT_VOLUME: {
      PointLightRecord rec_light {};
      rec_light.node_index = node_index;
      FillLightCommon(light, rec_light.common);

      rec_light.attenuation_model = atten_model;
      rec_light.decay_exponent = decay_exponent;

      if (const auto range = ResolveLightRange(light); range.has_value()) {
        rec_light.range = range.value();
      }
      if (const auto r = ResolveLightSourceRadius(light); r.has_value()) {
        rec_light.source_radius = r.value();
      }

      if (light.type != UFBX_LIGHT_POINT) {
        ++build.light_attr_skipped;
        ImportDiagnostic diag {
          .severity = ImportSeverity::kWarning,
          .code = "fbx.light.unsupported_type",
          .message = "unsupported FBX light type converted to point light",
          .source_path = request.source_path.string(),
          .object_path = std::string(name),
        };
        out.AddDiagnostic(std::move(diag));
      }

      build.point_lights.push_back(rec_light);
      break;
    }

    case UFBX_LIGHT_SPOT: {
      SpotLightRecord rec_light {};
      rec_light.node_index = node_index;
      FillLightCommon(light, rec_light.common);

      rec_light.attenuation_model = atten_model;
      rec_light.decay_exponent = decay_exponent;

      if (const auto range = ResolveLightRange(light); range.has_value()) {
        rec_light.range = range.value();
      }
      if (const auto r = ResolveLightSourceRadius(light); r.has_value()) {
        rec_light.source_radius = r.value();
      }

      const float inner = ToRadiansHeuristic(light.inner_angle);
      const float outer = ToRadiansHeuristic(light.outer_angle);
      rec_light.inner_cone_angle_radians = std::max(0.0F, inner);
      rec_light.outer_cone_angle_radians
        = std::max(rec_light.inner_cone_angle_radians, outer);

      build.spot_lights.push_back(rec_light);
      break;
    }

    default:
      ++build.light_attr_skipped;
      break;
    }
  }

  auto TraverseScene(const ufbx_scene& scene, const ImportRequest& request,
    CookedContentWriter& out, const std::vector<ImportedGeometry>& geometry,
    const std::string_view virtual_path, const ufbx_node* node,
    uint32_t parent_index, std::string_view parent_name, uint32_t& ordinal,
    SceneBuild& build) -> void
  {
    if (node == nullptr) {
      return;
    }

    const auto authored_name = fbx::ToStringView(node->name);
    const auto name
      = util::BuildSceneNodeName(authored_name, request, ordinal, parent_name);

    NodeRecord rec {};
    const auto node_virtual_path = std::string(virtual_path) + "/" + name;
    rec.node_id = MakeNodeKey(node_virtual_path);
    rec.scene_name_offset = AppendString(build.strings, name);
    rec.parent_index = parent_index;
    rec.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;

    const ufbx_transform local_trs = coord::ApplySwapYZIfEnabled(
      request.options.coordinate, node->local_transform);

    rec.translation[0] = util::ToFloat(local_trs.translation.x);
    rec.translation[1] = util::ToFloat(local_trs.translation.y);
    rec.translation[2] = util::ToFloat(local_trs.translation.z);

    rec.rotation[0] = util::ToFloat(local_trs.rotation.x);
    rec.rotation[1] = util::ToFloat(local_trs.rotation.y);
    rec.rotation[2] = util::ToFloat(local_trs.rotation.z);
    rec.rotation[3] = util::ToFloat(local_trs.rotation.w);

    rec.scale[0] = util::ToFloat(local_trs.scale.x);
    rec.scale[1] = util::ToFloat(local_trs.scale.y);
    rec.scale[2] = util::ToFloat(local_trs.scale.z);

    const auto index = static_cast<uint32_t>(build.nodes.size());
    if (index == 0) {
      rec.parent_index = 0;
    }

    build.nodes.push_back(rec);
    build.node_refs.push_back(SceneBuild::NodeRef {
      .node = node,
      .index = index,
      .name = name,
    });

    if (const auto geo_key = FindGeometryKey(node->mesh, geometry);
      geo_key.has_value()) {
      build.renderables.push_back(RenderableRecord {
        .node_index = index,
        .geometry_key = *geo_key,
        .visible = 1,
        .reserved = {},
      });
    }

    if (node->camera != nullptr) {
      AddCameraComponents(build, *node->camera, index, name);
    }

    if (node->light != nullptr) {
      AddLightComponents(build, *node->light, index, request, out, name);
    }

    ++ordinal;

    for (size_t i = 0; i < node->children.count; ++i) {
      TraverseScene(scene, request, out, geometry, virtual_path,
        node->children.data[i], index, name, ordinal, build);
    }
  }

  auto SortSceneComponents(SceneBuild& build) -> void
  {
    std::sort(build.renderables.begin(), build.renderables.end(),
      [](const RenderableRecord& a, const RenderableRecord& b) {
        return a.node_index < b.node_index;
      });

    std::sort(build.perspective_cameras.begin(),
      build.perspective_cameras.end(),
      [](const PerspectiveCameraRecord& a, const PerspectiveCameraRecord& b) {
        return a.node_index < b.node_index;
      });

    std::sort(build.orthographic_cameras.begin(),
      build.orthographic_cameras.end(),
      [](const OrthographicCameraRecord& a, const OrthographicCameraRecord& b) {
        return a.node_index < b.node_index;
      });

    std::sort(build.directional_lights.begin(), build.directional_lights.end(),
      [](const DirectionalLightRecord& a, const DirectionalLightRecord& b) {
        return a.node_index < b.node_index;
      });

    std::sort(build.point_lights.begin(), build.point_lights.end(),
      [](const PointLightRecord& a, const PointLightRecord& b) {
        return a.node_index < b.node_index;
      });

    std::sort(build.spot_lights.begin(), build.spot_lights.end(),
      [](const SpotLightRecord& a, const SpotLightRecord& b) {
        return a.node_index < b.node_index;
      });
  }

  auto LogSceneComponents(const SceneBuild& build) -> void
  {
    LOG_F(INFO,
      "Scene cameras: camera_attrs={} skipped_attrs={} perspective={} ortho={}",
      build.camera_attr_total, build.camera_attr_skipped,
      build.perspective_cameras.size(), build.orthographic_cameras.size());

    LOG_F(INFO,
      "Scene lights: light_attrs={} skipped_or_converted_attrs={} dir={} "
      "point={} "
      "spot={}",
      build.light_attr_total, build.light_attr_skipped,
      build.directional_lights.size(), build.point_lights.size(),
      build.spot_lights.size());
  }

  [[nodiscard]] auto SerializeScene(const std::string_view scene_name,
    const SceneBuild& build) -> std::vector<std::byte>
  {
    oxygen::serio::MemoryStream stream;
    oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);
    const auto packed = writer.ScopedAlignment(1);

    SceneAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
    util::TruncateAndNullTerminate(
      desc.header.name, sizeof(desc.header.name), scene_name);
    desc.header.version = oxygen::data::pak::kSceneAssetVersion;

    desc.nodes.offset = sizeof(SceneAssetDesc);
    desc.nodes.count = static_cast<uint32_t>(build.nodes.size());
    desc.nodes.entry_size = sizeof(NodeRecord);

    const auto nodes_bytes
      = std::as_bytes(std::span<const NodeRecord>(build.nodes));

    desc.scene_strings.offset
      = static_cast<oxygen::data::pak::StringTableOffsetT>(
        sizeof(SceneAssetDesc) + nodes_bytes.size());

    desc.scene_strings.size
      = static_cast<oxygen::data::pak::StringTableSizeT>(build.strings.size());

    const auto strings_bytes = std::span<const std::byte>(build.strings);

    std::vector<SceneComponentTableDesc> component_dir;
    component_dir.reserve(6);

    auto table_cursor = static_cast<oxygen::data::pak::OffsetT>(
      sizeof(SceneAssetDesc) + nodes_bytes.size() + strings_bytes.size());

    const auto add_table
      = [&](ComponentType type, size_t count, size_t entry_size) {
          component_dir.push_back(SceneComponentTableDesc {
      .component_type = static_cast<uint32_t>(type),
      .table = {
        .offset = table_cursor,
        .count = static_cast<uint32_t>(count),
        .entry_size = static_cast<uint32_t>(entry_size),
      },
    });
          table_cursor
            += static_cast<oxygen::data::pak::OffsetT>(count * entry_size);
        };

    if (!build.renderables.empty()) {
      add_table(ComponentType::kRenderable, build.renderables.size(),
        sizeof(RenderableRecord));
    }
    if (!build.perspective_cameras.empty()) {
      add_table(ComponentType::kPerspectiveCamera,
        build.perspective_cameras.size(), sizeof(PerspectiveCameraRecord));
    }
    if (!build.orthographic_cameras.empty()) {
      add_table(ComponentType::kOrthographicCamera,
        build.orthographic_cameras.size(), sizeof(OrthographicCameraRecord));
    }
    if (!build.directional_lights.empty()) {
      add_table(ComponentType::kDirectionalLight,
        build.directional_lights.size(), sizeof(DirectionalLightRecord));
    }
    if (!build.point_lights.empty()) {
      add_table(ComponentType::kPointLight, build.point_lights.size(),
        sizeof(PointLightRecord));
    }
    if (!build.spot_lights.empty()) {
      add_table(ComponentType::kSpotLight, build.spot_lights.size(),
        sizeof(SpotLightRecord));
    }

    desc.component_table_directory_offset = table_cursor;
    desc.component_table_count = static_cast<uint32_t>(component_dir.size());

    (void)writer.WriteBlob(
      std::as_bytes(std::span<const SceneAssetDesc, 1>(&desc, 1)));
    (void)writer.WriteBlob(nodes_bytes);
    (void)writer.WriteBlob(strings_bytes);

    if (!build.renderables.empty()) {
      (void)writer.WriteBlob(
        std::as_bytes(std::span<const RenderableRecord>(build.renderables)));
    }
    if (!build.perspective_cameras.empty()) {
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const PerspectiveCameraRecord>(build.perspective_cameras)));
    }
    if (!build.orthographic_cameras.empty()) {
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const OrthographicCameraRecord>(build.orthographic_cameras)));
    }
    if (!build.directional_lights.empty()) {
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const DirectionalLightRecord>(build.directional_lights)));
    }
    if (!build.point_lights.empty()) {
      (void)writer.WriteBlob(
        std::as_bytes(std::span<const PointLightRecord>(build.point_lights)));
    }
    if (!build.spot_lights.empty()) {
      (void)writer.WriteBlob(
        std::as_bytes(std::span<const SpotLightRecord>(build.spot_lights)));
    }
    if (!component_dir.empty()) {
      (void)writer.WriteBlob(
        std::as_bytes(std::span<const SceneComponentTableDesc>(component_dir)));
    }

    SceneEnvironmentBlockHeader env_header {};
    env_header.byte_size = sizeof(SceneEnvironmentBlockHeader);
    env_header.systems_count = 0;
    (void)writer.WriteBlob(std::as_bytes(
      std::span<const SceneEnvironmentBlockHeader, 1>(&env_header, 1)));

    const auto bytes = stream.Data();
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  }

} // namespace

auto WriteSceneAsset(const ufbx_scene& scene, const ImportRequest& request,
  CookedContentWriter& out, const std::vector<ImportedGeometry>& geometry,
  uint32_t& written_scenes) -> void
{
  const auto scene_name = util::BuildSceneName(request);
  const auto virtual_path
    = request.loose_cooked_layout.SceneVirtualPath(scene_name);
  const auto relpath
    = request.loose_cooked_layout.SceneDescriptorRelPath(scene_name);

  AssetKey scene_key {};
  switch (request.options.asset_key_policy) {
  case AssetKeyPolicy::kDeterministicFromVirtualPath:
    scene_key = util::MakeDeterministicAssetKey(virtual_path);
    break;
  case AssetKeyPolicy::kRandom:
    scene_key = util::MakeRandomAssetKey();
    break;
  }

  SceneBuild build;
  build.nodes.reserve(static_cast<size_t>(scene.nodes.count));
  build.strings.push_back(std::byte { 0 });

  build.renderables.reserve(static_cast<size_t>(scene.nodes.count));
  build.perspective_cameras.reserve(static_cast<size_t>(scene.nodes.count));
  build.orthographic_cameras.reserve(static_cast<size_t>(scene.nodes.count));
  build.directional_lights.reserve(static_cast<size_t>(scene.nodes.count));
  build.point_lights.reserve(static_cast<size_t>(scene.nodes.count));
  build.spot_lights.reserve(static_cast<size_t>(scene.nodes.count));

  build.node_refs.reserve(static_cast<size_t>(scene.nodes.count));

  uint32_t ordinal = 0;
  TraverseScene(scene, request, out, geometry, virtual_path, scene.root_node, 0,
    {}, ordinal, build);

  SortSceneComponents(build);
  LogSceneComponents(build);

  if (build.nodes.empty()) {
    NodeRecord root {};
    const auto root_name = std::string("root");
    root.node_id = MakeNodeKey(std::string(virtual_path) + "/" + root_name);
    root.scene_name_offset = AppendString(build.strings, root_name);
    root.parent_index = 0;
    root.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;
    build.nodes.push_back(root);
  }

  const auto bytes = SerializeScene(scene_name, build);

  LOG_F(INFO, "Emit scene '{}' -> {} (nodes={}, renderables={})",
    scene_name.c_str(), relpath.c_str(), build.nodes.size(),
    build.renderables.size());

  out.WriteAssetDescriptor(
    scene_key, AssetType::kScene, virtual_path, relpath, bytes);

  written_scenes += 1;
}

} // namespace oxygen::content::import::emit
