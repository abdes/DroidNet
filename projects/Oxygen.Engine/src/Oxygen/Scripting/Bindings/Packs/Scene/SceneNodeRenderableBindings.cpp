//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
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
    auto mesh = data::MeshBuilder(0, token)
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

    data::pak::GeometryAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kGeometry);
    desc.lod_count = 1;
    desc.bounding_box_min[0] = bbox_min.x;
    desc.bounding_box_min[1] = bbox_min.y;
    desc.bounding_box_min[2] = bbox_min.z;
    desc.bounding_box_max[0] = bbox_max.x;
    desc.bounding_box_max[1] = bbox_max.y;
    desc.bounding_box_max[2] = bbox_max.z;

    data::AssetKey key { .guid = data::GenerateAssetGuid() };
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
    if (token == "proc/arrow_gizmo" || token == "arrow_gizmo") {
      return "proc/arrow_gizmo";
    }
    if (token == "proc/sphere" || token == "sphere") {
      return "proc/sphere";
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
    return "proc/cube";
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
    } else if (canonical == "proc/arrow_gizmo") {
      mesh_data = data::MakeArrowGizmoMeshAsset();
    } else if (canonical == "proc/sphere") {
      mesh_data = data::MakeSphereMeshAsset();
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
        "SceneNodeRenderableBindings: failed to generate mesh for token '{}'; "
        "falling back to proc/cube",
        canonical);
      mesh_data = data::MakeCubeMeshAsset();
    }

    if (!mesh_data.has_value()) {
      throw std::runtime_error("failed to generate fallback cube mesh");
    }

    return BuildGeometryAssetFromMeshData(canonical, std::move(*mesh_data));
  }

  auto MakeMaterialForToken(const std::string& token)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    data::pak::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    desc.base_color[0] = 1.0F;
    desc.base_color[1] = 1.0F;
    desc.base_color[2] = 1.0F;
    desc.base_color[3] = 1.0F;
    desc.metalness = data::Unorm16 { 0.0F };
    desc.roughness = data::Unorm16 { 0.5F }; // NOLINT(*-magic-numbers)
    desc.ambient_occlusion = data::Unorm16 { 1.0F };
    data::AssetKey key { .guid = data::GenerateAssetGuid() };
    (void)token;
    return std::make_shared<const data::MaterialAsset>(key, desc);
  }

  auto GetOrCreateGeometryByToken(const std::string& token)
    -> std::shared_ptr<const data::GeometryAsset>
  {
    const auto canonical = CanonicalizeGeometryToken(token);
    auto& registry = GetRenderableAssetRegistry();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.geometry_by_token.find(canonical);
      it != registry.geometry_by_token.end()) {
      return it->second;
    }

    auto geometry = MakeGeometryForToken(canonical);
    registry.token_by_geometry_ptr.emplace(geometry.get(), canonical);
    registry.geometry_by_token.emplace(canonical, geometry);
    return geometry;
  }

  auto GetOrCreateMaterialByToken(const std::string& token)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    auto& registry = GetRenderableAssetRegistry();
    std::lock_guard lock(registry.mutex);
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
    std::lock_guard lock(registry.mutex);
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
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.token_by_material_ptr.find(material.get());
      it != registry.token_by_material_ptr.end()) {
      return it->second;
    }
    return {};
  }

  auto ReadPositiveLuaIndex(lua_State* state, const int index)
    -> std::optional<std::size_t>
  {
    const auto value = luaL_checkinteger(state, index);
    if (value < 1) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(value - 1);
  }

  auto TryGetAssetUserdataWithMetatable(lua_State* state, const int index,
    const char* metatable_name) -> AssetUserdata*
  {
    void* const raw = lua_touserdata(state, index);
    if (raw == nullptr) {
      return nullptr;
    }
    if (lua_getmetatable(state, index) == 0) {
      return nullptr;
    }
    luaL_getmetatable(state, metatable_name);
    const bool is_match = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    if (!is_match) {
      return nullptr;
    }
    return static_cast<AssetUserdata*>(raw);
  }

  auto SceneNodeRenderable(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->GetRenderable().HasGeometry()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeHasRenderable(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    lua_pushboolean(state, node->GetRenderable().HasGeometry() ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableHasGeometry(lua_State* state) -> int
  {
    return SceneNodeHasRenderable(state);
  }

  auto SceneNodeRenderableDetach(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
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
    auto* node = CheckSceneNode(state, 1);
    if (lua_type(state, 2) == LUA_TSTRING) {
      size_t len = 0;
      const char* token_raw = luaL_checklstring(state, 2, &len);
      const std::string token(token_raw, len);
      auto geometry = GetOrCreateGeometryByToken(token);
      node->GetRenderable().SetGeometry(std::move(geometry));
      const auto handle = node->GetHandle();
      LOG_F(INFO,
        "scene.node.renderable_set_geometry(token): token='{}' scene_id={} "
        "node_index={}",
        token, static_cast<unsigned>(handle.GetSceneId()),
        static_cast<unsigned>(handle.Index()));
      lua_pushboolean(state, 1);
      return 1;
    }

    auto* asset_user_data
      = TryGetAssetUserdataWithMetatable(state, 2, kGeometryAssetMetatableName);
    if (asset_user_data == nullptr
      || asset_user_data->kind != AssetUserdataKind::kGeometry
      || asset_user_data->geometry == nullptr) {
      luaL_argerror(
        state, 2, "geometry token string or GeometryAsset userdata expected");
      return 0;
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
    auto* node = CheckSceneNode(state, 1);
    const auto geometry = node->GetRenderable().GetGeometry();
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

  auto SceneNodeRenderableGetLodPolicy(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto renderable = node->GetRenderable();
    if (!renderable.HasGeometry()) {
      lua_pushnil(state);
      return 1;
    }

    lua_createtable(state, 0, 1);
    if (renderable.UsesFixedPolicy()) {
      lua_pushliteral(state, "fixed");
    } else if (renderable.UsesDistancePolicy()) {
      lua_pushliteral(state, "distance");
    } else if (renderable.UsesScreenSpaceErrorPolicy()) {
      lua_pushliteral(state, "screen_space_error");
    } else {
      lua_pushliteral(state, "unknown");
    }
    lua_setfield(state, -2, "kind");
    return 1;
  }

  auto SceneNodeRenderableSetLodPolicy(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    auto renderable = node->GetRenderable();
    if (!renderable.HasGeometry()) {
      lua_pushboolean(state, 0);
      return 1;
    }

    luaL_checktype(state, 2, LUA_TTABLE);
    lua_getfield(state, 2, "kind");
    size_t len = 0;
    const char* kind = luaL_checklstring(state, -1, &len);
    const auto kind_sv = std::string_view(kind, len);
    lua_pop(state, 1);

    if (kind_sv == "fixed") {
      scene::FixedPolicy policy {};
      lua_getfield(state, 2, "index");
      if (lua_isnumber(state, -1) != 0) {
        const auto raw = lua_tointeger(state, -1);
        policy.index = raw < 1 ? 0U : static_cast<std::size_t>(raw - 1);
      }
      lua_pop(state, 1);
      renderable.SetLodPolicy(policy);
      lua_pushboolean(state, 1);
      return 1;
    }

    if (kind_sv == "distance") {
      scene::DistancePolicy policy {};
      lua_getfield(state, 2, "hysteresis_ratio");
      if (lua_isnumber(state, -1) != 0) {
        policy.hysteresis_ratio = static_cast<float>(lua_tonumber(state, -1));
      }
      lua_pop(state, 1);

      lua_getfield(state, 2, "thresholds");
      if (lua_istable(state, -1) != 0) {
        const auto n = lua_objlen(state, -1);
        for (int i = 1; i <= n; ++i) {
          lua_rawgeti(state, -1, i);
          if (lua_isnumber(state, -1) != 0) {
            policy.thresholds.push_back(
              static_cast<float>(lua_tonumber(state, -1)));
          }
          lua_pop(state, 1);
        }
      }
      lua_pop(state, 1);
      renderable.SetLodPolicy(std::move(policy));
      lua_pushboolean(state, 1);
      return 1;
    }

    if (kind_sv == "screen_space_error") {
      scene::ScreenSpaceErrorPolicy policy {};

      lua_getfield(state, 2, "enter_finer_sse");
      if (lua_istable(state, -1) != 0) {
        const auto n = lua_objlen(state, -1);
        for (int i = 1; i <= n; ++i) {
          lua_rawgeti(state, -1, i);
          if (lua_isnumber(state, -1) != 0) {
            policy.enter_finer_sse.push_back(
              static_cast<float>(lua_tonumber(state, -1)));
          }
          lua_pop(state, 1);
        }
      }
      lua_pop(state, 1);

      lua_getfield(state, 2, "exit_coarser_sse");
      if (lua_istable(state, -1) != 0) {
        const auto n = lua_objlen(state, -1);
        for (int i = 1; i <= n; ++i) {
          lua_rawgeti(state, -1, i);
          if (lua_isnumber(state, -1) != 0) {
            policy.exit_coarser_sse.push_back(
              static_cast<float>(lua_tonumber(state, -1)));
          }
          lua_pop(state, 1);
        }
      }
      lua_pop(state, 1);

      renderable.SetLodPolicy(std::move(policy));
      lua_pushboolean(state, 1);
      return 1;
    }

    lua_pushboolean(state, 0);
    return 1;
  }

  auto SceneNodeRenderableGetActiveLodIndex(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (const auto index = node->GetRenderable().GetActiveLodIndex();
      index.has_value()) {
      lua_pushinteger(state, static_cast<lua_Integer>(*index + 1));
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeRenderableGetEffectiveLodCount(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    lua_pushinteger(state,
      static_cast<lua_Integer>(node->GetRenderable().EffectiveLodCount()));
    return 1;
  }

  auto SceneNodeRenderableIsSubmeshVisible(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto lod = ReadPositiveLuaIndex(state, 2);
    const auto submesh = ReadPositiveLuaIndex(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(
      state, node->GetRenderable().IsSubmeshVisible(*lod, *submesh) ? 1 : 0);
    return 1;
  }

  auto SceneNodeRenderableSetSubmeshVisible(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto lod = ReadPositiveLuaIndex(state, 2);
    const auto submesh = ReadPositiveLuaIndex(state, 3);
    luaL_checktype(state, 4, LUA_TBOOLEAN);
    const bool visible = lua_toboolean(state, 4) != 0;
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    node->GetRenderable().SetSubmeshVisible(*lod, *submesh, visible);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableSetAllSubmeshesVisible(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool visible = lua_toboolean(state, 2) != 0;
    node->GetRenderable().SetAllSubmeshesVisible(visible);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableSetMaterialOverride(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto lod = ReadPositiveLuaIndex(state, 2);
    const auto submesh = ReadPositiveLuaIndex(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }

    if (lua_isnil(state, 4) != 0) {
      node->GetRenderable().ClearMaterialOverride(*lod, *submesh);
      lua_pushboolean(state, 1);
      return 1;
    }

    if (lua_type(state, 4) == LUA_TSTRING) {
      size_t len = 0;
      const char* token_raw = luaL_checklstring(state, 4, &len);
      const std::string token(token_raw, len);
      auto material = GetOrCreateMaterialByToken(token);
      node->GetRenderable().SetMaterialOverride(
        *lod, *submesh, std::move(material));
      lua_pushboolean(state, 1);
      return 1;
    }

    auto* asset_user_data
      = TryGetAssetUserdataWithMetatable(state, 4, kMaterialAssetMetatableName);
    if (asset_user_data == nullptr
      || asset_user_data->kind != AssetUserdataKind::kMaterial
      || asset_user_data->material == nullptr) {
      luaL_argerror(state, 4,
        "material token string, MaterialAsset userdata, or nil expected");
      return 0;
    }

    auto material = asset_user_data->material;
    node->GetRenderable().SetMaterialOverride(*lod, *submesh, material);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableClearMaterialOverride(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto lod = ReadPositiveLuaIndex(state, 2);
    const auto submesh = ReadPositiveLuaIndex(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    node->GetRenderable().ClearMaterialOverride(*lod, *submesh);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeRenderableResolveSubmeshMaterial(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    const auto lod = ReadPositiveLuaIndex(state, 2);
    const auto submesh = ReadPositiveLuaIndex(state, 3);
    if (!lod.has_value() || !submesh.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto material
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
    auto* node = CheckSceneNode(state, 1);
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
    auto* node = CheckSceneNode(state, 1);
    const auto submesh = ReadPositiveLuaIndex(state, 2);
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
} // namespace

auto RegisterSceneNodeRenderableMethods(
  lua_State* state, const int metatable_index) -> void
{
  constexpr auto methods = std::to_array<luaL_Reg>({
    { .name = "renderable", .func = SceneNodeRenderable },
    { .name = "has_renderable", .func = SceneNodeHasRenderable },
    { .name = "renderable_has_geometry",
      .func = SceneNodeRenderableHasGeometry },
    { .name = "renderable_set_geometry",
      .func = SceneNodeRenderableSetGeometry },
    { .name = "renderable_get_geometry",
      .func = SceneNodeRenderableGetGeometry },
    { .name = "renderable_detach", .func = SceneNodeRenderableDetach },
    { .name = "renderable_get_lod_policy",
      .func = SceneNodeRenderableGetLodPolicy },
    { .name = "renderable_set_lod_policy",
      .func = SceneNodeRenderableSetLodPolicy },
    { .name = "renderable_get_active_lod_index",
      .func = SceneNodeRenderableGetActiveLodIndex },
    { .name = "renderable_get_effective_lod_count",
      .func = SceneNodeRenderableGetEffectiveLodCount },
    { .name = "renderable_is_submesh_visible",
      .func = SceneNodeRenderableIsSubmeshVisible },
    { .name = "renderable_set_submesh_visible",
      .func = SceneNodeRenderableSetSubmeshVisible },
    { .name = "renderable_set_all_submeshes_visible",
      .func = SceneNodeRenderableSetAllSubmeshesVisible },
    { .name = "renderable_set_material_override",
      .func = SceneNodeRenderableSetMaterialOverride },
    { .name = "renderable_clear_material_override",
      .func = SceneNodeRenderableClearMaterialOverride },
    { .name = "renderable_resolve_submesh_material",
      .func = SceneNodeRenderableResolveSubmeshMaterial },
    { .name = "renderable_get_world_bounding_sphere",
      .func = SceneNodeRenderableGetWorldBoundingSphere },
    { .name = "renderable_get_world_submesh_aabb",
      .func = SceneNodeRenderableGetWorldSubmeshAabb },
  });

  for (const auto& reg : methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, metatable_index, reg.name);
  }
}

} // namespace oxygen::scripting::bindings
