//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <lua.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  using MeshData = std::pair<std::vector<data::Vertex>, std::vector<uint32_t>>;

  auto BuildGeometryAssetFromMeshData(const std::string& token, MeshData data)
    -> std::shared_ptr<const data::GeometryAsset>
  {
    auto [vertices, indices] = std::move(data);
    auto default_material = data::MaterialAsset::CreateDefault();
    auto mesh = data::MeshBuilder(data::LodIndex {}, token)
                  .WithVertices(vertices)
                  .WithIndices(indices)
                  .BeginSubMesh("main", std::move(default_material))
                  .WithMeshView({
                    .first_index = 0,
                    .index_count = static_cast<uint32_t>(indices.size()),
                    .first_vertex = 0,
                    .vertex_count = static_cast<uint32_t>(vertices.size()),
                  })
                  .EndSubMesh()
                  .Build();

    Vec3 bbox_min { 0.0F, 0.0F, 0.0F };
    Vec3 bbox_max { 0.0F, 0.0F, 0.0F };
    if (!vertices.empty()) {
      bbox_min = vertices.front().position;
      bbox_max = vertices.front().position;
      for (const auto& vertex : vertices) {
        bbox_min.x = (std::min)(bbox_min.x, vertex.position.x);
        bbox_min.y = (std::min)(bbox_min.y, vertex.position.y);
        bbox_min.z = (std::min)(bbox_min.z, vertex.position.z);
        bbox_max.x = (std::max)(bbox_max.x, vertex.position.x);
        bbox_max.y = (std::max)(bbox_max.y, vertex.position.y);
        bbox_max.z = (std::max)(bbox_max.z, vertex.position.z);
      }
    }

    data::pak::geometry::GeometryAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kGeometry);
    desc.lod_count = 1;
    desc.bounding_box_min[0] = bbox_min.x;
    desc.bounding_box_min[1] = bbox_min.y;
    desc.bounding_box_min[2] = bbox_min.z;
    desc.bounding_box_max[0] = bbox_max.x;
    desc.bounding_box_max[1] = bbox_max.y;
    desc.bounding_box_max[2] = bbox_max.z;

    const auto key = MakeProceduralGeometryKey(token);
    std::vector<std::shared_ptr<data::Mesh>> lods;
    lods.emplace_back(std::shared_ptr<data::Mesh>(std::move(mesh)));
    return std::make_shared<const data::GeometryAsset>(
      key, desc, std::move(lods));
  }

  auto CanonicalizeGeometryToken(std::string_view token) -> std::string
  {
    if (token == "proc/cube" || token == "cube") {
      return "proc/cube";
    }
    if (token == "proc/subdivided_cube" || token == "subdivided_cube") {
      return "proc/subdivided_cube";
    }
    if (token == "proc/arrow_gizmo" || token == "arrow_gizmo") {
      return "proc/arrow_gizmo";
    }
    if (token == "proc/sphere" || token == "sphere") {
      return "proc/sphere";
    }
    if (token == "proc/capsule" || token == "capsule") {
      return "proc/capsule";
    }
    if (token == "proc/icosphere" || token == "icosphere") {
      return "proc/icosphere";
    }
    if (token == "proc/plane" || token == "plane") {
      return "proc/plane";
    }
    if (token == "proc/cylinder" || token == "cylinder") {
      return "proc/cylinder";
    }
    if (token == "proc/cone" || token == "cone") {
      return "proc/cone";
    }
    if (token == "proc/torus" || token == "torus") {
      return "proc/torus";
    }
    if (token == "proc/quad" || token == "quad") {
      return "proc/quad";
    }
    return std::string(token);
  }

  struct RenderableAssetRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const data::GeometryAsset>>
      geometry_by_token;
    std::unordered_map<const data::GeometryAsset*, std::string>
      token_by_geometry_ptr;
    std::unordered_map<std::string, std::shared_ptr<const data::MaterialAsset>>
      material_by_token;
    std::unordered_map<const data::MaterialAsset*, std::string>
      token_by_material_ptr;
  };

  auto GetRenderableAssetRegistry() -> RenderableAssetRegistry&
  {
    static RenderableAssetRegistry registry {};
    return registry;
  }

  auto MakeGeometryForToken(const std::string& token)
    -> std::shared_ptr<const data::GeometryAsset>
  {
    const auto canonical = CanonicalizeGeometryToken(token);
    std::optional<MeshData> mesh_data;

    if (canonical == "proc/cube") {
      mesh_data = data::MakeCubeMeshAsset();
    } else if (canonical == "proc/subdivided_cube") {
      mesh_data = data::MakeSubdividedCubeMeshAsset();
    } else if (canonical == "proc/arrow_gizmo") {
      mesh_data = data::MakeArrowGizmoMeshAsset();
    } else if (canonical == "proc/sphere") {
      mesh_data = data::MakeSphereMeshAsset();
    } else if (canonical == "proc/capsule") {
      mesh_data = data::MakeCapsuleMeshAsset();
    } else if (canonical == "proc/icosphere") {
      mesh_data = data::MakeIcoSphereMeshAsset();
    } else if (canonical == "proc/plane") {
      mesh_data = data::MakePlaneMeshAsset();
    } else if (canonical == "proc/cylinder") {
      mesh_data = data::MakeCylinderMeshAsset();
    } else if (canonical == "proc/cone") {
      mesh_data = data::MakeConeMeshAsset();
    } else if (canonical == "proc/torus") {
      mesh_data = data::MakeTorusMeshAsset();
    } else if (canonical == "proc/quad") {
      mesh_data = data::MakeQuadMeshAsset();
    }

    if (!mesh_data.has_value()) {
      LOG_F(WARNING,
        "SceneNodeRenderableBindings: rejected unsupported or invalid "
        "geometry token '{}'",
        canonical);
      return {};
    }

    return BuildGeometryAssetFromMeshData(canonical, std::move(*mesh_data));
  }

  auto MakeMaterialForToken(const std::string& token)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    data::pak::render::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    desc.base_color[0] = 1.0F;
    desc.base_color[1] = 1.0F;
    desc.base_color[2] = 1.0F;
    desc.base_color[3] = 1.0F;
    desc.metalness = data::Unorm16 { 0.0F };
    desc.roughness = data::Unorm16 { 0.5F };
    desc.ambient_occlusion = data::Unorm16 { 1.0F };
    const auto key = MakeProceduralMaterialKey(token);
    return std::make_shared<const data::MaterialAsset>(key, desc);
  }

  auto GetOrCreateGeometryByToken(const std::string& token)
    -> std::shared_ptr<const data::GeometryAsset>
  {
    const auto canonical = CanonicalizeGeometryToken(token);
    auto& registry = GetRenderableAssetRegistry();
    std::scoped_lock lock(registry.mutex);
    if (const auto it = registry.geometry_by_token.find(canonical);
      it != registry.geometry_by_token.end()) {
      return it->second;
    }

    auto geometry = MakeGeometryForToken(canonical);
    if (!geometry) {
      return {};
    }
    registry.token_by_geometry_ptr.emplace(geometry.get(), canonical);
    registry.geometry_by_token.emplace(canonical, geometry);
    return geometry;
  }

  auto GetOrCreateMaterialByToken(const std::string& token)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    auto& registry = GetRenderableAssetRegistry();
    std::scoped_lock lock(registry.mutex);
    if (const auto it = registry.material_by_token.find(token);
      it != registry.material_by_token.end()) {
      return it->second;
    }

    auto material = MakeMaterialForToken(token);
    registry.token_by_material_ptr.emplace(material.get(), token);
    registry.material_by_token.emplace(token, material);
    return material;
  }

  auto GeometryTokenFromAsset(
    const std::shared_ptr<const data::GeometryAsset>& geometry) -> std::string
  {
    auto& registry = GetRenderableAssetRegistry();
    std::scoped_lock lock(registry.mutex);
    if (const auto it = registry.token_by_geometry_ptr.find(geometry.get());
      it != registry.token_by_geometry_ptr.end()) {
      return it->second;
    }
    return {};
  }

  auto MaterialTokenFromAsset(
    const std::shared_ptr<const data::MaterialAsset>& material) -> std::string
  {
    auto& registry = GetRenderableAssetRegistry();
    std::scoped_lock lock(registry.mutex);
    if (const auto it = registry.token_by_material_ptr.find(material.get());
      it != registry.token_by_material_ptr.end()) {
      return it->second;
    }
    return {};
  }

  //! Reads a 1-based Lua index as a strong-typed 0-based geometry index.
  /*!
   Rejects non-numbers, values below 1 and values the index type cannot hold.
  */
  template <typename Index>
  auto ReadLuaIndexAs(lua_State* state, const int index) -> std::optional<Index>
  {
    using Raw = Index::UnderlyingType;
    if (lua_isnumber(state, index) == 0) {
      return std::nullopt;
    }
    const auto value = lua_tointeger(state, index);
    if (value < 1
      || std::cmp_greater(value - 1, std::numeric_limits<Raw>::max())) {
      return std::nullopt;
    }
    return Index { static_cast<Raw>(value - 1) };
  }

  auto TryGetAssetUserdata(lua_State* state, const int index) -> AssetUserdata*
  {
    if (lua_type(state, index) != LUA_TUSERDATA) {
      return nullptr;
    }
    if (lua_userdatatag(state, index) != kTagAsset) {
      return nullptr;
    }
    return static_cast<AssetUserdata*>(lua_touserdata(state, index));
  }

  auto SceneNodeRenderable(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!node->GetRenderable().HasGeometry()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeHasRenderable(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->GetRenderable().HasGeometry() ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableHasGeometry(lua_State* state) -> int
  {
    return SceneNodeHasRenderable(state);
  }

  auto SceneNodeRenderableDetach(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto handle = node->GetHandle();
    const auto node_name = node->GetName();
    const bool ok = node->GetRenderable().Detach();
    LOG_F(INFO,
      "scene.node.renderable_detach: name='{}' scene_id={} node_index={} ok={}",
      node_name, static_cast<unsigned>(handle.GetSceneId()),
      static_cast<unsigned>(handle.Index()), ok);
    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableSetGeometry(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) == LUA_TSTRING) {
      size_t len = 0;
      const char* token_raw = lua_tolstring(state, 2, &len);
      if (token_raw == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
      }
      const std::string token(token_raw, len);
      auto geometry = GetOrCreateGeometryByToken(token);
      if (!geometry) {
        lua_pushboolean(state, 0);
        return 1;
      }
      node->GetRenderable().SetGeometry(std::move(geometry));
      const auto handle = node->GetHandle();
      DLOG_F(3,
        "scene.node.renderable_set_geometry(token): token='{}' scene_id={} "
        "node_index={}",
        token, static_cast<unsigned>(handle.GetSceneId()),
        static_cast<unsigned>(handle.Index()));
      lua_pushboolean(state, 1);
      return 1;
    }

    auto* asset_user_data = TryGetAssetUserdata(state, 2);
    if (asset_user_data == nullptr
      || asset_user_data->kind != AssetUserdataKind::kGeometry
      || asset_user_data->geometry == nullptr) {
      LOG_F(WARNING,
        "scene.node.renderable_set_geometry rejected invalid geometry "
        "userdata/type");
      lua_pushboolean(state, 0);
      return 1;
    }

    auto geometry = asset_user_data->geometry;
    node->GetRenderable().SetGeometry(geometry);
    const auto handle = node->GetHandle();
    LOG_F(INFO,
      "scene.node.renderable_set_geometry(asset): geom_ptr={} scene_id={} "
      "node_index={}",
      static_cast<const void*>(geometry.get()),
      static_cast<unsigned>(handle.GetSceneId()),
      static_cast<unsigned>(handle.Index()));
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableGetGeometry(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    auto geometry = node->GetRenderable().GetGeometry();
    if (!geometry) {
      lua_pushnil(state);
      return 1;
    }
    const auto token = GeometryTokenFromAsset(geometry);
    if (!token.empty()) {
      lua_pushlstring(state, token.data(), token.size());
      return 1;
    }
    return PushGeometryAsset(state, std::move(geometry));
  }

  //! The numbers of the list in field `name` of the table at `index`.
  auto ReadFloatList(lua_State* state, const int index, const char* name)
    -> std::vector<float>
  {
    auto values = std::vector<float> {};
    lua_getfield(state, index, name);
    if (lua_istable(state, -1) != 0) {
      const auto n = lua_objlen(state, -1);
      for (int i = 1; i <= n; ++i) {
        lua_rawgeti(state, -1, i);
        if (lua_isnumber(state, -1) != 0) {
          values.push_back(static_cast<float>(lua_tonumber(state, -1)));
        }
        lua_pop(state, 1);
      }
    }
    lua_pop(state, 1);
    return values;
  }

  //! Sets field `name` of the table on top of the stack to `values`.
  auto SetFloatListField(lua_State* state, const char* name,
    const std::vector<float>& values) -> void
  {
    lua_createtable(state, static_cast<int>(values.size()), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
      lua_pushnumber(state, values[i]);
      lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    lua_setfield(state, -2, name);
  }

  //! The LOD policy the table at `index` describes (`LodPolicy`).
  auto ReadLodPolicy(lua_State* state, const int index)
    -> std::optional<scene::LodPolicy>
  {
    if (lua_type(state, index) != LUA_TTABLE) {
      return std::nullopt;
    }
    lua_getfield(state, index, "kind");
    size_t len = 0;
    const char* kind = lua_tolstring(state, -1, &len);
    const auto kind_sv
      = kind == nullptr ? std::string_view {} : std::string_view(kind, len);
    lua_pop(state, 1);

    if (kind_sv == "fixed") {
      scene::FixedPolicy policy {};
      lua_getfield(state, index, "index");
      if (lua_isnumber(state, -1) != 0) {
        policy.index
          = ReadLuaIndexAs<data::LodIndex>(state, -1).value_or(policy.index);
      }
      lua_pop(state, 1);
      return policy;
    }
    if (kind_sv == "distance") {
      scene::DistancePolicy policy {};
      lua_getfield(state, index, "hysteresis_ratio");
      if (lua_isnumber(state, -1) != 0) {
        policy.hysteresis_ratio = static_cast<float>(lua_tonumber(state, -1));
      }
      lua_pop(state, 1);
      policy.thresholds = ReadFloatList(state, index, "thresholds");
      return policy;
    }
    if (kind_sv == "screen_space_error") {
      return scene::ScreenSpaceErrorPolicy {
        .enter_finer_sse = ReadFloatList(state, index, "enter_finer_sse"),
        .exit_coarser_sse = ReadFloatList(state, index, "exit_coarser_sse"),
      };
    }
    return std::nullopt;
  }

  //! Pushes `policy` as a `LodPolicy` table.
  auto PushLodPolicy(lua_State* state, const scene::LodPolicy& policy) -> void
  {
    lua_createtable(state, 0, 3);
    if (const auto* fixed = std::get_if<scene::FixedPolicy>(&policy)) {
      lua_pushliteral(state, "fixed");
      lua_setfield(state, -2, "kind");
      lua_pushinteger(state, static_cast<lua_Integer>(fixed->index.get() + 1));
      lua_setfield(state, -2, "index");
    } else if (const auto* distance
      = std::get_if<scene::DistancePolicy>(&policy)) {
      lua_pushliteral(state, "distance");
      lua_setfield(state, -2, "kind");
      lua_pushnumber(state, distance->hysteresis_ratio);
      lua_setfield(state, -2, "hysteresis_ratio");
      SetFloatListField(state, "thresholds", distance->thresholds);
    } else if (const auto* sse
      = std::get_if<scene::ScreenSpaceErrorPolicy>(&policy)) {
      lua_pushliteral(state, "screen_space_error");
      lua_setfield(state, -2, "kind");
      SetFloatListField(state, "enter_finer_sse", sse->enter_finer_sse);
      SetFloatListField(state, "exit_coarser_sse", sse->exit_coarser_sse);
    }
  }

  //! The SurfaceId whose canonical UUID text is at `index`.
  auto ReadSurfaceId(lua_State* state, const int index)
    -> std::optional<data::SurfaceId>
  {
    if (lua_type(state, index) != LUA_TSTRING) {
      return std::nullopt;
    }
    size_t len = 0;
    const char* text = lua_tolstring(state, index, &len);
    auto surface = data::SurfaceId::FromString(std::string_view(text, len));
    if (!surface.has_value() || surface->IsNil()) {
      return std::nullopt;
    }
    return *surface;
  }

  auto SceneNodeRenderableGetLodPolicy(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto policy = node->GetRenderable().GetLodPolicy();
    if (!policy.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    PushLodPolicy(state, *policy);
    return 1;
  }

  auto SceneNodeRenderableSetLodPolicyOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto renderable = node->GetRenderable();
    auto policy = ReadLodPolicy(state, 2);
    if (!renderable.HasGeometry() || !policy.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    renderable.SetLodPolicyOverride(std::move(*policy));
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableClearLodPolicyOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto renderable = node->GetRenderable();
    if (!renderable.HasGeometry()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    renderable.ClearLodPolicyOverride();
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableGetActiveLodIndex(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (const auto index = node->GetRenderable().GetActiveLodIndex();
      index.has_value()) {
      lua_pushinteger(state, static_cast<lua_Integer>(index->get() + 1));
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeRenderableGetEffectiveLodCount(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    lua_pushinteger(state,
      static_cast<lua_Integer>(node->GetRenderable().EffectiveLodCount()));
    return 1;
  }

  auto SceneNodeRenderableIsSubmeshVisible(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto lod = ReadLuaIndexAs<data::LodIndex>(state, 2);
    const auto submesh = ReadLuaIndexAs<data::SubmeshIndex>(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().IsSubmeshVisible(*lod, *submesh) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableSubmeshCastsShadows(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto lod = ReadLuaIndexAs<data::LodIndex>(state, 2);
    const auto submesh = ReadLuaIndexAs<data::SubmeshIndex>(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().SubmeshCastsShadows(*lod, *submesh) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableGetSurfaceIds(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto geometry = node->GetRenderable().GetGeometry();
    if (!geometry) {
      lua_pushnil(state);
      return 1;
    }
    const auto& surfaces = geometry->Surfaces().surfaces;
    lua_createtable(state, static_cast<int>(surfaces.size()), 0);
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
      const auto text = data::to_string(surfaces[i].surface_id);
      lua_pushlstring(state, text.data(), text.size());
      lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    return 1;
  }

  auto SceneNodeRenderableSetMaterialOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }

    if (lua_isnil(state, 3) != 0) {
      lua_pushboolean(
        state, node->GetRenderable().ClearMaterialOverride(*surface) ? 1 : 0);
      return 1;
    }

    if (lua_type(state, 3) == LUA_TSTRING) {
      size_t len = 0;
      const char* token_raw = lua_tolstring(state, 3, &len);
      if (token_raw == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
      }
      const std::string token(token_raw, len);
      auto material = GetOrCreateMaterialByToken(token);
      lua_pushboolean(state,
        node->GetRenderable().SetMaterialOverride(*surface, std::move(material))
          ? 1
          : 0);
      return 1;
    }

    auto* asset_user_data = TryGetAssetUserdata(state, 3);
    if (asset_user_data == nullptr
      || asset_user_data->kind != AssetUserdataKind::kMaterial
      || asset_user_data->material == nullptr) {
      LOG_F(WARNING,
        "scene.node.renderable_set_material_override rejected invalid material "
        "userdata/type");
      lua_pushboolean(state, 0);
      return 1;
    }

    lua_pushboolean(state,
      node->GetRenderable().SetMaterialOverride(
        *surface, asset_user_data->material)
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeRenderableClearMaterialOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().ClearMaterialOverride(*surface) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableSetVisibilityOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()
      || lua_type(state, 3) != LUA_TBOOLEAN) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const bool visible = lua_toboolean(state, 3) != 0;
    lua_pushboolean(state,
      node->GetRenderable().SetVisibilityOverride(*surface, visible) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableClearVisibilityOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().ClearVisibilityOverride(*surface) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableSetCastsShadowsOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()
      || lua_type(state, 3) != LUA_TBOOLEAN) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const bool casts_shadows = lua_toboolean(state, 3) != 0;
    lua_pushboolean(state,
      node->GetRenderable().SetCastsShadowsOverride(*surface, casts_shadows)
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeRenderableClearCastsShadowsOverride(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    const auto surface = ReadSurfaceId(state, 2);
    if (node == nullptr || !surface.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().ClearCastsShadowsOverride(*surface) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableResolveSubmeshMaterial(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto lod = ReadLuaIndexAs<data::LodIndex>(state, 2);
    const auto submesh = ReadLuaIndexAs<data::SubmeshIndex>(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    auto material
      = node->GetRenderable().ResolveSubmeshMaterial(*lod, *submesh);
    if (!material) {
      lua_pushnil(state);
      return 1;
    }
    const auto token = MaterialTokenFromAsset(material);
    if (!token.empty()) {
      lua_pushlstring(state, token.data(), token.size());
      return 1;
    }
    return PushMaterialAsset(state, std::move(material));
  }

  auto SceneNodeRenderableGetWorldBoundingSphere(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto sphere = node->GetRenderable().GetWorldBoundingSphere();
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, sphere.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, sphere.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, sphere.z);
    lua_setfield(state, -2, "z");
    lua_pushnumber(state, sphere.w);
    lua_setfield(state, -2, "w");
    return 1;
  }

  auto SceneNodeRenderableGetWorldSubmeshAabb(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto submesh = ReadLuaIndexAs<data::SubmeshIndex>(state, 2);
    if (!submesh.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto aabb
      = node->GetRenderable().GetWorldSubMeshBoundingBox(*submesh);
    if (!aabb.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 2);
    PushVec3(state, aabb->first);
    lua_setfield(state, -2, "min");
    PushVec3(state, aabb->second);
    lua_setfield(state, -2, "max");
    return 1;
  }

  constexpr auto kRenderableMethods = std::to_array<BindingFunction>({
    {
      .name = "renderable",
      .function = SceneNodeRenderable,
      .signature = "() -> SceneNode?",
    },
    {
      .name = "has_renderable",
      .function = SceneNodeHasRenderable,
      .signature = "() -> boolean",
    },
    {
      .name = "renderable_has_geometry",
      .function = SceneNodeRenderableHasGeometry,
      .signature = "() -> boolean",
    },
    {
      .name = "renderable_set_geometry",
      .function = SceneNodeRenderableSetGeometry,
      .signature = "(geometry: string | GeometryAsset) -> boolean",
    },
    {
      .name = "renderable_get_geometry",
      .function = SceneNodeRenderableGetGeometry,
      .signature = "() -> string?",
    },
    {
      .name = "renderable_detach",
      .function = SceneNodeRenderableDetach,
      .signature = "() -> boolean",
    },
    {
      .name = "renderable_get_lod_policy",
      .function = SceneNodeRenderableGetLodPolicy,
      .signature = "() -> LodPolicy?",
    },
    {
      .name = "renderable_set_lod_policy_override",
      .function = SceneNodeRenderableSetLodPolicyOverride,
      .signature = "(policy: LodPolicy) -> boolean",
    },
    {
      .name = "renderable_clear_lod_policy_override",
      .function = SceneNodeRenderableClearLodPolicyOverride,
      .signature = "() -> boolean",
    },
    {
      .name = "renderable_get_active_lod_index",
      .function = SceneNodeRenderableGetActiveLodIndex,
      .signature = "() -> number?",
    },
    {
      .name = "renderable_get_effective_lod_count",
      .function = SceneNodeRenderableGetEffectiveLodCount,
      .signature = "() -> number",
    },
    {
      .name = "renderable_get_surface_ids",
      .function = SceneNodeRenderableGetSurfaceIds,
      .signature = "() -> { string }?",
    },
    {
      .name = "renderable_set_material_override",
      .function = SceneNodeRenderableSetMaterialOverride,
      .signature
      = "(surface: string, material: (string | MaterialAsset)?) -> boolean",
    },
    {
      .name = "renderable_clear_material_override",
      .function = SceneNodeRenderableClearMaterialOverride,
      .signature = "(surface: string) -> boolean",
    },
    {
      .name = "renderable_set_visibility_override",
      .function = SceneNodeRenderableSetVisibilityOverride,
      .signature = "(surface: string, visible: boolean) -> boolean",
    },
    {
      .name = "renderable_clear_visibility_override",
      .function = SceneNodeRenderableClearVisibilityOverride,
      .signature = "(surface: string) -> boolean",
    },
    {
      .name = "renderable_set_casts_shadows_override",
      .function = SceneNodeRenderableSetCastsShadowsOverride,
      .signature = "(surface: string, casts_shadows: boolean) -> boolean",
    },
    {
      .name = "renderable_clear_casts_shadows_override",
      .function = SceneNodeRenderableClearCastsShadowsOverride,
      .signature = "(surface: string) -> boolean",
    },
    {
      .name = "renderable_is_submesh_visible",
      .function = SceneNodeRenderableIsSubmeshVisible,
      .signature = "(lod: number, submesh: number) -> boolean",
    },
    {
      .name = "renderable_submesh_casts_shadows",
      .function = SceneNodeRenderableSubmeshCastsShadows,
      .signature = "(lod: number, submesh: number) -> boolean",
    },
    {
      .name = "renderable_resolve_submesh_material",
      .function = SceneNodeRenderableResolveSubmeshMaterial,
      .signature = "(lod: number, submesh: number) -> string?",
    },
    {
      .name = "renderable_get_world_bounding_sphere",
      .function = SceneNodeRenderableGetWorldBoundingSphere,
      .signature = "() -> BoundingSphere?",
    },
    {
      .name = "renderable_get_world_submesh_aabb",
      .function = SceneNodeRenderableGetWorldSubmeshAabb,
      .signature = "(submesh: number) -> Aabb?",
    },
  });

  constexpr auto kRenderableClasses = std::to_array<BindingClass>({
    {
      .name = "SceneNode",
      .metatable = kSceneNodeMetatable,
      .methods = kRenderableMethods,
    },
  });

  constexpr auto kRenderableTypes = std::to_array<BindingTypeAlias>({
    {
      .name = "LodPolicyKind",
      .definition = R"("fixed" | "distance" | "screen_space_error")",
    },
    {
      .name = "LodPolicy",
      .definition = "{ kind: LodPolicyKind, index: number?, "
                    "hysteresis_ratio: number?, thresholds: { number }?, "
                    "enter_finer_sse: { number }?, "
                    "exit_coarser_sse: { number }? }",
    },
    {
      .name = "BoundingSphere",
      .definition = "{ x: number, y: number, z: number, w: number }",
    },
    { .name = "Aabb", .definition = "{ min: vector, max: vector }" },
  });

  constexpr auto kRenderableDescription
    = std::to_array<BindingNamespaceDescription>({
      {
        .path = "scene",
        .classes = kRenderableClasses,
        .types = kRenderableTypes,
      },
    });
} // namespace

auto RegisterSceneNodeRenderableMethods(
  lua_State* state, const int metatable_index) -> void
{
  RegisterFunctions(state, metatable_index, kRenderableMethods);
}

auto DescribeSceneNodeRenderableBindings()
  -> std::span<const BindingNamespaceDescription>
{
  return kRenderableDescription;
}

} // namespace oxygen::scripting::bindings
