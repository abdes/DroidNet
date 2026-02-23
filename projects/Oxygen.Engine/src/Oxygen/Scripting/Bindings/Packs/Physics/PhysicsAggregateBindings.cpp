//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
// GetActiveEventPhase is used by the phase-rejection helpers below.
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsAggregateBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>

namespace oxygen::scripting::bindings {

namespace {

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.aggregate.{} rejected during fixed_simulation",
      op_name);
    lua_pushnil(state);
    return true;
  }

  auto RejectFixedSimulationWithBool(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.aggregate.{} rejected during fixed_simulation",
      op_name);
    lua_pushboolean(state, 0);
    return true;
  }

  auto RejectMutationOutsideGameplayWithNil(
    lua_State* state, const char* op_name) -> bool
  {
    if (IsAggregateMutationAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.aggregate.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushnil(state);
    return true;
  }

  auto RejectMutationOutsideGameplayWithBool(
    lua_State* state, const char* op_name) -> bool
  {
    if (IsAggregateMutationAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.aggregate.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushboolean(state, 0);
    return true;
  }

  auto LuaAggregateCreate(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "create")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithNil(state, "create")) {
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto result
      = physics_module->Aggregates().CreateAggregate(*world_id_opt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushAggregateHandle(state, *world_id_opt, result.value());
  }

  auto LuaAggregateDestroy(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "destroy")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "destroy")) {
      return 1;
    }

    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Aggregates().DestroyAggregate(
      handle->world_id, handle->aggregate_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaAggregateAddMemberBody(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "add_member_body")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "add_member_body")) {
      return 1;
    }

    const auto* handle = CheckAggregateHandle(state, 1);
    const auto body_id = ParseBodyIdOrHandle(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Aggregates().AddMemberBody(
      handle->world_id, handle->aggregate_id, body_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaAggregateRemoveMemberBody(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "remove_member_body")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "remove_member_body")) {
      return 1;
    }

    const auto* handle = CheckAggregateHandle(state, 1);
    const auto body_id = ParseBodyIdOrHandle(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Aggregates().RemoveMemberBody(
      handle->world_id, handle->aggregate_id, body_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaAggregateGetMemberBodies(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_member_bodies")) {
      return 1;
    }

    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    size_t capacity = 16;
    for (int i = 0; i < 8; ++i) {
      std::vector<physics::BodyId> members(capacity, physics::kInvalidBodyId);
      auto result
        = physics_module->Aggregates().GetMemberBodies(handle->world_id,
          handle->aggregate_id, std::span<physics::BodyId> { members });
      if (!result.has_value()) {
        if (result.error() == physics::PhysicsError::kBufferTooSmall) {
          capacity = (std::min)(capacity * 2U, size_t { 4096 });
          continue;
        }
        lua_pushnil(state);
        return 1;
      }

      const auto count = (std::min)(result.value(), members.size());
      lua_createtable(state, static_cast<int>(count), 0);
      for (size_t idx = 0; idx < count; ++idx) {
        PushBodyId(state, members[idx]);
        lua_rawseti(state, -2, static_cast<int>(idx + 1));
      }
      return 1;
    }

    lua_pushnil(state);
    return 1;
  }

  auto LuaAggregateFlushStructuralChanges(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "flush_structural_changes")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithNil(
          state, "flush_structural_changes")) {
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto result
      = physics_module->Aggregates().FlushStructuralChanges(*world_id_opt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(result.value()));
    return 1;
  }

} // namespace

auto RegisterPhysicsAggregateBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  RegisterPhysicsAggregateHandleMetatable(state);
  RegisterPhysicsAggregateIdMetatable(state);

  luaL_getmetatable(state, kPhysicsAggregateHandleMetatable);
  lua_pushcfunction(
    state, LuaAggregateDestroy, "physics.aggregate_handle.destroy");
  lua_setfield(state, -2, "destroy");
  lua_pushcfunction(state, LuaAggregateAddMemberBody,
    "physics.aggregate_handle.add_member_body");
  lua_setfield(state, -2, "add_member_body");
  lua_pushcfunction(state, LuaAggregateRemoveMemberBody,
    "physics.aggregate_handle.remove_member_body");
  lua_setfield(state, -2, "remove_member_body");
  lua_pushcfunction(state, LuaAggregateGetMemberBodies,
    "physics.aggregate_handle.get_member_bodies");
  lua_setfield(state, -2, "get_member_bodies");
  lua_pop(state, 1);

  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto aggregate_index
    = PushOxygenSubtable(state, physics_index, "aggregate");

  lua_pushcfunction(state, LuaAggregateCreate, "physics.aggregate.create");
  lua_setfield(state, aggregate_index, "create");
  lua_pushcfunction(state, LuaAggregateDestroy, "physics.aggregate.destroy");
  lua_setfield(state, aggregate_index, "destroy");
  lua_pushcfunction(
    state, LuaAggregateAddMemberBody, "physics.aggregate.add_member_body");
  lua_setfield(state, aggregate_index, "add_member_body");
  lua_pushcfunction(state, LuaAggregateRemoveMemberBody,
    "physics.aggregate.remove_member_body");
  lua_setfield(state, aggregate_index, "remove_member_body");
  lua_pushcfunction(
    state, LuaAggregateGetMemberBodies, "physics.aggregate.get_member_bodies");
  lua_setfield(state, aggregate_index, "get_member_bodies");
  lua_pushcfunction(state, LuaAggregateFlushStructuralChanges,
    "physics.aggregate.flush_structural_changes");
  lua_setfield(state, aggregate_index, "flush_structural_changes");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
