//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <string_view>
#include <type_traits>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Shape/ShapeDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsShapeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto ParseShapeDesc(lua_State* state, const int arg_index)
    -> physics::shape::ShapeDesc
  {
    physics::shape::ShapeDesc desc {};
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "shape");
    if (lua_istable(state, -1) != 0) {
      desc.geometry = ParseCollisionShape(state, -1);
      lua_pop(state, 1);
    } else {
      lua_pop(state, 1);
      desc.geometry = ParseCollisionShape(state, desc_index);
    }

    lua_getfield(state, desc_index, "local_position");
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, desc.local_position)) {
        luaL_error(state, "local_position must be a vector");
        return {};
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "local_rotation");
    if (lua_isnil(state, -1) == 0) {
      const auto* q = TryCheckQuat(state, -1);
      if (q == nullptr) {
        luaL_error(state, "local_rotation must be a quaternion");
        return {};
      }
      desc.local_rotation = Quat { q->w, q->x, q->y, q->z };
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "local_scale");
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, desc.local_scale)) {
        luaL_error(state, "local_scale must be a vector");
        return {};
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "is_sensor");
    if (lua_isnil(state, -1) == 0) {
      luaL_checktype(state, -1, LUA_TBOOLEAN);
      desc.is_sensor = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "collision_own_layer");
    if (lua_isnil(state, -1) == 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw <= 0) {
        luaL_error(state, "collision_own_layer must be > 0");
      }
      desc.collision_own_layer = static_cast<uint64_t>(raw);
      if ((desc.collision_own_layer & (desc.collision_own_layer - 1ULL)) != 0) {
        luaL_error(state, "collision_own_layer must have exactly one bit set");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "collision_target_layers");
    if (lua_isnil(state, -1) == 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw < 0) {
        luaL_error(state, "collision_target_layers must be non-negative");
      }
      desc.collision_target_layers = static_cast<uint64_t>(raw);
    }
    lua_pop(state, 1);

    return desc;
  }

  auto PushPayloadTypeString(
    lua_State* state, const physics::ShapePayloadType payload_type) -> void
  {
    switch (payload_type) {
    case physics::ShapePayloadType::kConvex:
      lua_pushstring(state, "convex");
      return;
    case physics::ShapePayloadType::kMesh:
      lua_pushstring(state, "mesh");
      return;
    case physics::ShapePayloadType::kHeightField:
      lua_pushstring(state, "height_field");
      return;
    case physics::ShapePayloadType::kCompound:
      lua_pushstring(state, "compound");
      return;
    default:
      lua_pushstring(state, "invalid");
      return;
    }
  }

  auto PushCookedPayload(
    lua_State* state, const physics::CookedShapePayload& payload) -> void
  {
    lua_createtable(state, 0, 2);
    PushPayloadTypeString(state, payload.payload_type);
    lua_setfield(state, -2, "payload_type");
    lua_pushlstring(state, reinterpret_cast<const char*>(payload.data.data()),
      payload.data.size());
    lua_setfield(state, -2, "data");
  }

  auto PushShapeGeometryTable(
    lua_State* state, const physics::CollisionShape& shape) -> void
  {
    lua_createtable(state, 0, 6);
    std::visit(
      [state](const auto& geometry) {
        using T = std::decay_t<decltype(geometry)>;
        if constexpr (std::is_same_v<T, physics::SphereShape>) {
          lua_pushstring(state, "sphere");
          lua_setfield(state, -2, "type");
          lua_pushnumber(state, geometry.radius);
          lua_setfield(state, -2, "radius");
        } else if constexpr (std::is_same_v<T, physics::BoxShape>) {
          lua_pushstring(state, "box");
          lua_setfield(state, -2, "type");
          PushVec3(state, geometry.extents);
          lua_setfield(state, -2, "extents");
        } else if constexpr (std::is_same_v<T, physics::CapsuleShape>) {
          lua_pushstring(state, "capsule");
          lua_setfield(state, -2, "type");
          lua_pushnumber(state, geometry.radius);
          lua_setfield(state, -2, "radius");
          lua_pushnumber(state, geometry.half_height);
          lua_setfield(state, -2, "half_height");
        } else if constexpr (std::is_same_v<T, physics::CylinderShape>) {
          lua_pushstring(state, "cylinder");
          lua_setfield(state, -2, "type");
          lua_pushnumber(state, geometry.radius);
          lua_setfield(state, -2, "radius");
          lua_pushnumber(state, geometry.half_height);
          lua_setfield(state, -2, "half_height");
        } else if constexpr (std::is_same_v<T, physics::ConeShape>) {
          lua_pushstring(state, "cone");
          lua_setfield(state, -2, "type");
          lua_pushnumber(state, geometry.radius);
          lua_setfield(state, -2, "radius");
          lua_pushnumber(state, geometry.half_height);
          lua_setfield(state, -2, "half_height");
          PushCookedPayload(state, geometry.cooked_payload);
          lua_setfield(state, -2, "cooked_payload");
        } else if constexpr (std::is_same_v<T, physics::ConvexHullShape>) {
          lua_pushstring(state, "convex_hull");
          lua_setfield(state, -2, "type");
          PushCookedPayload(state, geometry.cooked_payload);
          lua_setfield(state, -2, "cooked_payload");
        } else if constexpr (std::is_same_v<T, physics::TriangleMeshShape>) {
          lua_pushstring(state, "triangle_mesh");
          lua_setfield(state, -2, "type");
          PushCookedPayload(state, geometry.cooked_payload);
          lua_setfield(state, -2, "cooked_payload");
        } else if constexpr (std::is_same_v<T, physics::HeightFieldShape>) {
          lua_pushstring(state, "height_field");
          lua_setfield(state, -2, "type");
          PushCookedPayload(state, geometry.cooked_payload);
          lua_setfield(state, -2, "cooked_payload");
        } else if constexpr (std::is_same_v<T, physics::PlaneShape>) {
          lua_pushstring(state, "plane");
          lua_setfield(state, -2, "type");
          PushVec3(state, geometry.normal);
          lua_setfield(state, -2, "normal");
          lua_pushnumber(state, geometry.distance);
          lua_setfield(state, -2, "distance");
        } else if constexpr (std::is_same_v<T, physics::WorldBoundaryShape>) {
          lua_pushstring(state, "world_boundary");
          lua_setfield(state, -2, "type");
          const char* mode_text = "invalid";
          if (geometry.mode == physics::WorldBoundaryMode::kAabbClamp) {
            mode_text = "aabb_clamp";
          } else if (geometry.mode == physics::WorldBoundaryMode::kPlaneSet) {
            mode_text = "plane_set";
          }
          lua_pushstring(state, mode_text);
          lua_setfield(state, -2, "boundary_mode");
          PushVec3(state, geometry.limits_min);
          lua_setfield(state, -2, "limits_min");
          PushVec3(state, geometry.limits_max);
          lua_setfield(state, -2, "limits_max");
        } else if constexpr (std::is_same_v<T, physics::CompoundShape>) {
          lua_pushstring(state, "compound");
          lua_setfield(state, -2, "type");
          PushCookedPayload(state, geometry.cooked_payload);
          lua_setfield(state, -2, "cooked_payload");
        }
      },
      shape);
  }

  auto PushShapeDescTable(
    lua_State* state, const physics::shape::ShapeDesc& desc) -> int
  {
    lua_createtable(state, 0, 7);
    PushShapeGeometryTable(state, desc.geometry);
    lua_setfield(state, -2, "shape");
    PushVec3(state, desc.local_position);
    lua_setfield(state, -2, "local_position");
    PushQuat(state, desc.local_rotation.x, desc.local_rotation.y,
      desc.local_rotation.z, desc.local_rotation.w);
    lua_setfield(state, -2, "local_rotation");
    PushVec3(state, desc.local_scale);
    lua_setfield(state, -2, "local_scale");
    lua_pushboolean(state, desc.is_sensor ? 1 : 0);
    lua_setfield(state, -2, "is_sensor");
    lua_pushinteger(state, static_cast<lua_Integer>(desc.collision_own_layer));
    lua_setfield(state, -2, "collision_own_layer");
    lua_pushinteger(
      state, static_cast<lua_Integer>(desc.collision_target_layers));
    lua_setfield(state, -2, "collision_target_layers");
    return 1;
  }

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(
      WARNING, "physics.shape.{} rejected during fixed_simulation", op_name);
    lua_pushnil(state);
    return true;
  }

  auto LuaShapeCreate(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "create")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.shape.create rejected outside gameplay/scene_mutation "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    const auto desc = ParseShapeDesc(state, 1);
    const auto result = physics_module->Shapes().CreateShape(desc);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushShapeId(state, result.value());
  }

  auto LuaShapeDestroy(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "destroy")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.shape.destroy rejected outside gameplay/scene_mutation "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* id = CheckShapeId(state, 1);
    const auto result = physics_module->Shapes().DestroyShape(id->shape_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaShapeGet(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto* id = CheckShapeId(state, 1);
    const auto result = physics_module->Shapes().GetShapeDesc(id->shape_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushShapeDescTable(state, result.value());
  }

  auto LuaShapeIdGetId(lua_State* state) -> int
  {
    const auto* id = CheckShapeId(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(id->shape_id.get()));
    return 1;
  }

} // namespace

auto RegisterPhysicsShapeBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  RegisterPhysicsShapeIdMetatable(state);
  RegisterPhysicsShapeInstanceIdMetatable(state);

  luaL_getmetatable(state, kPhysicsShapeIdMetatable);
  lua_pushcfunction(state, LuaShapeIdGetId, "physics.shape_id.get_id");
  lua_setfield(state, -2, "get_id");
  lua_pop(state, 1);

  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto shape_index = PushOxygenSubtable(state, physics_index, "shape");

  lua_pushcfunction(state, LuaShapeCreate, "physics.shape.create");
  lua_setfield(state, shape_index, "create");
  lua_pushcfunction(state, LuaShapeGet, "physics.shape.get");
  lua_setfield(state, shape_index, "get");
  lua_pushcfunction(state, LuaShapeDestroy, "physics.shape.destroy");
  lua_setfield(state, shape_index, "destroy");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
