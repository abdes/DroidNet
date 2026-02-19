//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentProceduralBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  using MeshData = std::pair<std::vector<data::Vertex>, std::vector<uint32_t>>;

  auto ReadOptionalUInt(lua_State* state, const int table_index,
    const char* field, const unsigned int fallback_value) -> unsigned int
  {
    lua_getfield(state, table_index, field);
    if (lua_isnumber(state, -1) != 0) {
      const auto raw = lua_tointeger(state, -1);
      lua_pop(state, 1);
      if (raw <= 0) {
        return fallback_value;
      }
      return static_cast<unsigned int>(raw);
    }
    lua_pop(state, 1);
    return fallback_value;
  }

  auto ReadOptionalFloat(lua_State* state, const int table_index,
    const char* field, const float fallback_value) -> float
  {
    lua_getfield(state, table_index, field);
    if (lua_isnumber(state, -1) != 0) {
      const float value = static_cast<float>(lua_tonumber(state, -1));
      lua_pop(state, 1);
      return value;
    }
    lua_pop(state, 1);
    return fallback_value;
  }

  auto BuildGeometryAsset(const std::string& name, MeshData mesh_data)
    -> std::shared_ptr<data::GeometryAsset>
  {
    auto [vertices, indices] = std::move(mesh_data);
    auto material = data::MaterialAsset::CreateDefault();
    auto mesh = data::MeshBuilder(0, name)
                  .WithVertices(vertices)
                  .WithIndices(indices)
                  .BeginSubMesh("main", std::move(material))
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
    return std::make_shared<data::GeometryAsset>(key, desc, std::move(lods));
  }

  auto GeneratePrimitive(lua_State* state, const std::string_view kind)
    -> std::optional<MeshData>
  {
    if (kind == "cube") {
      return data::MakeCubeMeshAsset();
    }
    if (kind == "arrow_gizmo") {
      return data::MakeArrowGizmoMeshAsset();
    }

    if (lua_gettop(state) < 2 || lua_isnil(state, 2) != 0) {
      if (kind == "sphere") {
        return data::MakeSphereMeshAsset();
      }
      if (kind == "plane") {
        return data::MakePlaneMeshAsset();
      }
      if (kind == "cylinder") {
        return data::MakeCylinderMeshAsset();
      }
      if (kind == "cone") {
        return data::MakeConeMeshAsset();
      }
      if (kind == "torus") {
        return data::MakeTorusMeshAsset();
      }
      if (kind == "quad") {
        return data::MakeQuadMeshAsset();
      }
      return std::nullopt;
    }

    luaL_checktype(state, 2, LUA_TTABLE);
    if (kind == "sphere") {
      const auto latitude = ReadOptionalUInt(state, 2, "latitude_segments", 16);
      const auto longitude
        = ReadOptionalUInt(state, 2, "longitude_segments", 32);
      return data::MakeSphereMeshAsset(latitude, longitude);
    }
    if (kind == "plane") {
      const auto x_segments = ReadOptionalUInt(state, 2, "x_segments", 1);
      const auto z_segments = ReadOptionalUInt(state, 2, "z_segments", 1);
      const float size = ReadOptionalFloat(state, 2, "size", 1.0F);
      return data::MakePlaneMeshAsset(x_segments, z_segments, size);
    }
    if (kind == "cylinder") {
      const auto segments = ReadOptionalUInt(state, 2, "segments", 32);
      const float height = ReadOptionalFloat(state, 2, "height", 1.0F);
      const float radius = ReadOptionalFloat(state, 2, "radius", 0.5F);
      return data::MakeCylinderMeshAsset(segments, height, radius);
    }
    if (kind == "cone") {
      const auto segments = ReadOptionalUInt(state, 2, "segments", 32);
      const float height = ReadOptionalFloat(state, 2, "height", 1.0F);
      const float radius = ReadOptionalFloat(state, 2, "radius", 0.5F);
      return data::MakeConeMeshAsset(segments, height, radius);
    }
    if (kind == "torus") {
      const auto major_segments
        = ReadOptionalUInt(state, 2, "major_segments", 32);
      const auto minor_segments
        = ReadOptionalUInt(state, 2, "minor_segments", 16);
      const float major_radius
        = ReadOptionalFloat(state, 2, "major_radius", 1.0F);
      const float minor_radius
        = ReadOptionalFloat(state, 2, "minor_radius", 0.25F);
      return data::MakeTorusMeshAsset(
        major_segments, minor_segments, major_radius, minor_radius);
    }
    if (kind == "quad") {
      const float width = ReadOptionalFloat(state, 2, "width", 1.0F);
      const float height = ReadOptionalFloat(state, 2, "height", 1.0F);
      return data::MakeQuadMeshAsset(width, height);
    }
    return std::nullopt;
  }

  auto AssetsCreateProceduralGeometry(lua_State* state) -> int
  {
    size_t kind_len = 0;
    const char* kind_raw = luaL_checklstring(state, 1, &kind_len);
    const std::string_view kind(kind_raw, kind_len);
    const auto mesh_data = GeneratePrimitive(state, kind);
    if (!mesh_data.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    std::string name("proc/");
    name.append(kind);
    auto geometry = BuildGeometryAsset(name, *mesh_data);
    return PushGeometryAsset(state, std::move(geometry));
  }

  auto AssetsCreateDefaultMaterial(lua_State* state) -> int
  {
    auto material = data::MaterialAsset::CreateDefault();
    return PushMaterialAsset(state, std::move(material));
  }

  auto AssetsCreateDebugMaterial(lua_State* state) -> int
  {
    auto material = data::MaterialAsset::CreateDebug();
    return PushMaterialAsset(state, std::move(material));
  }

  auto AssetsCreateMaterialWithBaseColor(lua_State* state) -> int
  {
    const float r = static_cast<float>(luaL_checknumber(state, 1));
    const float g = static_cast<float>(luaL_checknumber(state, 2));
    const float b = static_cast<float>(luaL_checknumber(state, 3));
    const float a = static_cast<float>(luaL_optnumber(state, 4, 1.0));

    auto sanitize_channel = [](const float value) {
      if (!std::isfinite(value)) {
        return 1.0F;
      }
      return std::clamp(value, 0.0F, 1.0F);
    };

    data::pak::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    desc.base_color[0] = sanitize_channel(r);
    desc.base_color[1] = sanitize_channel(g);
    desc.base_color[2] = sanitize_channel(b);
    desc.base_color[3] = sanitize_channel(a);
    desc.metalness = data::Unorm16 { 0.0F };
    desc.roughness = data::Unorm16 { 0.5F }; // NOLINT(*-magic-numbers)
    desc.ambient_occlusion = data::Unorm16 { 1.0F };

    data::AssetKey key { .guid = data::GenerateAssetGuid() };
    auto material = std::make_shared<const data::MaterialAsset>(key, desc);
    return PushMaterialAsset(state, std::move(material));
  }
} // namespace

auto RegisterContentModuleProcedural(lua_State* state, const int module_index)
  -> void
{
  lua_pushcfunction(
    state, AssetsCreateProceduralGeometry, "assets.create_procedural_geometry");
  lua_setfield(state, module_index, "create_procedural_geometry");
  lua_pushcfunction(
    state, AssetsCreateDefaultMaterial, "assets.create_default_material");
  lua_setfield(state, module_index, "create_default_material");
  lua_pushcfunction(
    state, AssetsCreateDebugMaterial, "assets.create_debug_material");
  lua_setfield(state, module_index, "create_debug_material");
  lua_pushcfunction(state, AssetsCreateMaterialWithBaseColor,
    "assets.create_material_with_base_color");
  lua_setfield(state, module_index, "create_material_with_base_color");
}

} // namespace oxygen::scripting::bindings
