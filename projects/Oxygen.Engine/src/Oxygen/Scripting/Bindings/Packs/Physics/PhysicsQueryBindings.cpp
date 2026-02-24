//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Query/Sweep.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsQueryBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(
      WARNING, "physics.query.{} rejected during fixed_simulation", op_name);
    lua_pushnil(state);
    return true;
  }

  auto RejectFixedSimulationWithNilPair(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(
      WARNING, "physics.query.{} rejected during fixed_simulation", op_name);
    lua_pushnil(state);
    lua_pushnil(state);
    return true;
  }

  auto ParseCollisionMaskField(lua_State* state, const int table_index,
    const char* field_name) -> physics::CollisionMask
  {
    lua_getfield(state, table_index, field_name);
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      return physics::kCollisionMaskAll;
    }
    const auto raw = luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    if (raw < 0) {
      luaL_error(state, "%s must be non-negative", field_name);
      return physics::kCollisionMaskAll;
    }
    return physics::CollisionMask { static_cast<uint32_t>(raw) };
  }

  auto ParseDirectionField(lua_State* state, const int table_index) -> Vec3
  {
    lua_getfield(state, table_index, "direction");
    Vec3 direction = oxygen::space::move::Forward;
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, direction)) {
        luaL_error(state, "query direction must be a vector");
        lua_pop(state, 1);
        return oxygen::space::move::Forward;
      }
    }
    lua_pop(state, 1);

    const auto len_sq = (direction.x * direction.x)
      + (direction.y * direction.y) + (direction.z * direction.z);
    if (!std::isfinite(len_sq) || len_sq <= 1.0e-8F) {
      luaL_error(state, "query direction must be finite and non-zero");
      return oxygen::space::move::Forward;
    }
    const auto inv_len = 1.0F / std::sqrt(len_sq);
    return Vec3 {
      direction.x * inv_len,
      direction.y * inv_len,
      direction.z * inv_len,
    };
  }

  auto ParseIgnoreBodiesField(lua_State* state, const int table_index)
    -> std::vector<physics::BodyId>
  {
    lua_getfield(state, table_index, "ignore_bodies");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      return {};
    }
    const auto body_ids = ParseBodyIdArray(state, -1);
    lua_pop(state, 1);
    return body_ids;
  }

  auto ParseRaycastDesc(lua_State* state, const int arg_index,
    std::vector<physics::BodyId>& ignore_bodies) -> physics::query::RaycastDesc
  {
    physics::query::RaycastDesc desc {};
    if (lua_isnoneornil(state, arg_index) == 0) {
      const auto desc_index = lua_absindex(state, arg_index);
      luaL_checktype(state, desc_index, LUA_TTABLE);

      lua_getfield(state, desc_index, "origin");
      if (lua_isnil(state, -1) == 0) {
        if (!TryCheckVec3(state, -1, desc.origin)) {
          lua_pop(state, 1);
          luaL_error(state, "raycast origin must be a vector");
          return {};
        }
      }
      lua_pop(state, 1);

      desc.direction = ParseDirectionField(state, desc_index);

      lua_getfield(state, desc_index, "max_distance");
      if (lua_isnil(state, -1) == 0) {
        desc.max_distance = static_cast<float>(luaL_checknumber(state, -1));
      }
      lua_pop(state, 1);
      if (!std::isfinite(desc.max_distance) || desc.max_distance <= 0.0F) {
        luaL_error(state, "raycast max_distance must be positive and finite");
      }

      desc.collision_mask
        = ParseCollisionMaskField(state, desc_index, "collision_mask");
      ignore_bodies = ParseIgnoreBodiesField(state, desc_index);
      desc.ignore_bodies = std::span<const physics::BodyId> { ignore_bodies };
    }
    return desc;
  }

  auto ParseSweepDesc(lua_State* state, const int arg_index,
    physics::PhysicsModule& physics_module,
    std::vector<physics::BodyId>& ignore_bodies) -> physics::query::SweepDesc
  {
    physics::query::SweepDesc desc {};
    if (lua_isnoneornil(state, arg_index) == 0) {
      const auto desc_index = lua_absindex(state, arg_index);
      luaL_checktype(state, desc_index, LUA_TTABLE);

      lua_getfield(state, desc_index, "shape_id");
      if (lua_isnil(state, -1) == 0) {
        const auto* shape_id = CheckShapeId(state, -1);
        const auto shape_result
          = physics_module.Shapes().GetShapeDesc(shape_id->shape_id);
        if (!shape_result.has_value()) {
          lua_pop(state, 1);
          luaL_error(state, "invalid shape_id in sweep descriptor");
        }
        desc.shape = shape_result.value().geometry;
      }
      lua_pop(state, 1);

      lua_getfield(state, desc_index, "shape");
      if (lua_isnil(state, -1) == 0) {
        desc.shape = ParseCollisionShape(state, -1);
      }
      lua_pop(state, 1);

      lua_getfield(state, desc_index, "origin");
      if (lua_isnil(state, -1) == 0) {
        if (!TryCheckVec3(state, -1, desc.origin)) {
          lua_pop(state, 1);
          luaL_error(state, "sweep origin must be a vector");
          return {};
        }
      }
      lua_pop(state, 1);

      desc.direction = ParseDirectionField(state, desc_index);

      lua_getfield(state, desc_index, "max_distance");
      if (lua_isnil(state, -1) == 0) {
        desc.max_distance = static_cast<float>(luaL_checknumber(state, -1));
      }
      lua_pop(state, 1);
      if (!std::isfinite(desc.max_distance) || desc.max_distance <= 0.0F) {
        luaL_error(state, "sweep max_distance must be positive and finite");
      }

      desc.collision_mask
        = ParseCollisionMaskField(state, desc_index, "collision_mask");
      ignore_bodies = ParseIgnoreBodiesField(state, desc_index);
      desc.ignore_bodies = std::span<const physics::BodyId> { ignore_bodies };
    }
    return desc;
  }

  auto ParseOverlapDesc(lua_State* state, const int arg_index,
    physics::PhysicsModule& physics_module,
    std::vector<physics::BodyId>& ignore_bodies) -> physics::query::OverlapDesc
  {
    physics::query::OverlapDesc desc {};
    if (lua_isnoneornil(state, arg_index) == 0) {
      const auto desc_index = lua_absindex(state, arg_index);
      luaL_checktype(state, desc_index, LUA_TTABLE);

      lua_getfield(state, desc_index, "shape_id");
      if (lua_isnil(state, -1) == 0) {
        const auto* shape_id = CheckShapeId(state, -1);
        const auto shape_result
          = physics_module.Shapes().GetShapeDesc(shape_id->shape_id);
        if (!shape_result.has_value()) {
          lua_pop(state, 1);
          luaL_error(state, "invalid shape_id in overlap descriptor");
        }
        desc.shape = shape_result.value().geometry;
      }
      lua_pop(state, 1);

      lua_getfield(state, desc_index, "shape");
      if (lua_isnil(state, -1) == 0) {
        desc.shape = ParseCollisionShape(state, -1);
      }
      lua_pop(state, 1);

      lua_getfield(state, desc_index, "center");
      if (lua_isnil(state, -1) == 0) {
        if (!TryCheckVec3(state, -1, desc.center)) {
          lua_pop(state, 1);
          luaL_error(state, "overlap center must be a vector");
          return {};
        }
      }
      lua_pop(state, 1);

      desc.collision_mask
        = ParseCollisionMaskField(state, desc_index, "collision_mask");
      ignore_bodies = ParseIgnoreBodiesField(state, desc_index);
      desc.ignore_bodies = std::span<const physics::BodyId> { ignore_bodies };
    }
    return desc;
  }

  auto ParseMaxHits(lua_State* state, const int arg_index) -> int
  {
    if (lua_isnoneornil(state, arg_index) != 0) {
      return 64;
    }
    const auto max_hits = static_cast<int>(luaL_checkinteger(state, arg_index));
    if (max_hits <= 0) {
      luaL_error(state, "max_hits must be > 0");
      return 64;
    }
    return std::min(max_hits, 1024);
  }

  auto PushOptionalNodeFromBodyId(lua_State* state,
    physics::PhysicsModule& module, const physics::BodyId body_id) -> int
  {
    const auto node_handle = module.GetNodeForBodyId(body_id);
    if (!node_handle.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto frame_context = GetActiveFrameContext(state);
    if (frame_context == nullptr || frame_context->GetScene() == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    const auto node = frame_context->GetScene()->GetNode(*node_handle);
    if (!node.has_value() || !node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSceneNode(state, *node);
  }

  auto PushRaycastHitTable(lua_State* state, physics::PhysicsModule& module,
    const physics::query::RaycastHit& hit) -> int
  {
    lua_createtable(state, 0, 5);
    PushBodyId(state, hit.body_id);
    lua_setfield(state, -2, "body_id");
    PushVec3(state, hit.position);
    lua_setfield(state, -2, "position");
    PushVec3(state, hit.normal);
    lua_setfield(state, -2, "normal");
    lua_pushnumber(state, hit.distance);
    lua_setfield(state, -2, "distance");
    PushOptionalNodeFromBodyId(state, module, hit.body_id);
    lua_setfield(state, -2, "node");
    return 1;
  }

  auto LuaPhysicsQueryRaycast(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "raycast")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    std::vector<physics::BodyId> ignore_bodies {};
    const auto desc = ParseRaycastDesc(state, 1, ignore_bodies);

    // IQueryApi::Raycast returns a RaycastHit with body_id. PushRaycastHitTable
    // then resolves the scene node via GetNodeForBodyId(BodyId) which is part
    // of the public testing surface, achieving Tier A (node field populated for
    // all scene-mapped bodies, nil for aggregate-owned bodies).
    const auto result = physics_module->Queries().Raycast(*world_id_opt, desc);
    if (!result.has_value() || !result.value().has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushRaycastHitTable(state, *physics_module, *result.value());
  }

  auto LuaPhysicsQuerySweep(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "sweep")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    std::vector<physics::BodyId> ignore_bodies {};
    const auto desc = ParseSweepDesc(state, 1, *physics_module, ignore_bodies);
    const auto max_hits = ParseMaxHits(state, 2);
    std::vector<physics::query::SweepHit> hits(static_cast<size_t>(max_hits));

    const auto result = physics_module->Queries().Sweep(
      *world_id_opt, desc, std::span { hits });
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto count = std::min<int>(
      static_cast<int>(result.value()), static_cast<int>(hits.size()));
    lua_createtable(state, count, 0);
    for (int i = 0; i < count; ++i) {
      const auto& hit = hits[static_cast<size_t>(i)];
      lua_createtable(state, 0, 4);
      PushBodyId(state, hit.body_id);
      lua_setfield(state, -2, "body_id");
      PushVec3(state, hit.position);
      lua_setfield(state, -2, "position");
      PushVec3(state, hit.normal);
      lua_setfield(state, -2, "normal");
      lua_pushnumber(state, hit.distance);
      lua_setfield(state, -2, "distance");
      lua_rawseti(state, -2, i + 1);
    }
    return 1;
  }

  auto LuaPhysicsQueryOverlap(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNilPair(state, "overlap")) {
      return 2;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      lua_pushnil(state);
      return 2;
    }

    std::vector<physics::BodyId> ignore_bodies {};
    const auto desc
      = ParseOverlapDesc(state, 1, *physics_module, ignore_bodies);
    const auto max_hits = ParseMaxHits(state, 2);
    std::vector<uint64_t> user_data(static_cast<size_t>(max_hits), 0);

    const auto result = physics_module->Queries().Overlap(
      *world_id_opt, desc, std::span<uint64_t> { user_data });
    if (!result.has_value()) {
      lua_pushnil(state);
      lua_pushnil(state);
      return 2;
    }

    const auto count = std::min<int>(
      static_cast<int>(result.value()), static_cast<int>(user_data.size()));
    lua_pushinteger(state, count);
    lua_createtable(state, count, 0);
    for (int i = 0; i < count; ++i) {
      PushBodyId(state,
        physics::BodyId {
          static_cast<uint32_t>(user_data[static_cast<size_t>(i)]),
        });
      lua_rawseti(state, -2, i + 1);
    }
    return 2;
  }

} // namespace

auto RegisterQueryBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto query_index = PushOxygenSubtable(state, physics_index, "query");

  lua_pushcfunction(state, LuaPhysicsQueryRaycast, "physics.query.raycast");
  lua_setfield(state, query_index, "raycast");
  lua_pushcfunction(state, LuaPhysicsQuerySweep, "physics.query.sweep");
  lua_setfield(state, query_index, "sweep");
  lua_pushcfunction(state, LuaPhysicsQueryOverlap, "physics.query.overlap");
  lua_setfield(state, query_index, "overlap");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
