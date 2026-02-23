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
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Articulation/ArticulationDesc.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
// GetActiveEventPhase is used by the phase-rejection helpers below.
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
// GetActiveEventPhase is used by the phase-rejection helpers below.
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>

namespace oxygen::scripting::bindings {

namespace {

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.articulation.{} rejected during fixed_simulation",
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
    LOG_F(WARNING, "physics.articulation.{} rejected during fixed_simulation",
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
      "physics.articulation.{} rejected outside gameplay phase "
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
      "physics.articulation.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushboolean(state, 0);
    return true;
  }

  auto ParseCreateDesc(lua_State* state, const int arg_index)
    -> physics::articulation::ArticulationDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "root_body_id");
    const auto root_body = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);
    return physics::articulation::ArticulationDesc {
      .root_body_id = root_body,
    };
  }

  auto ParseLinkDesc(lua_State* state, const int arg_index)
    -> physics::articulation::ArticulationLinkDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "parent_body_id");
    const auto parent = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, desc_index, "child_body_id");
    const auto child = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);

    return physics::articulation::ArticulationLinkDesc {
      .parent_body_id = parent,
      .child_body_id = child,
    };
  }

  auto LuaArticulationCreate(lua_State* state) -> int
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
    const auto desc = ParseCreateDesc(state, 1);

    const auto result
      = physics_module->Articulations().CreateArticulation(*world_id_opt, desc);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushAggregateHandle(state, *world_id_opt, result.value());
  }

  auto LuaArticulationDestroy(lua_State* state) -> int
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
    const auto result = physics_module->Articulations().DestroyArticulation(
      handle->world_id, handle->aggregate_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaArticulationAddLink(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "add_link")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "add_link")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    const auto desc = ParseLinkDesc(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Articulations().AddLink(
      handle->world_id, handle->aggregate_id, desc);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaArticulationRemoveLink(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "remove_link")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "remove_link")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    const auto child_body_id = ParseBodyIdOrHandle(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Articulations().RemoveLink(
      handle->world_id, handle->aggregate_id, child_body_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaArticulationGetRootBody(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_root_body")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->Articulations().GetRootBody(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushBodyId(state, result.value());
  }

  auto LuaArticulationGetLinkBodies(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_link_bodies")) {
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
      std::vector<physics::BodyId> bodies(capacity, physics::kInvalidBodyId);
      const auto result
        = physics_module->Articulations().GetLinkBodies(handle->world_id,
          handle->aggregate_id, std::span<physics::BodyId> { bodies });
      if (!result.has_value()) {
        if (result.error() == physics::PhysicsError::kBufferTooSmall) {
          capacity = (std::min)(capacity * 2U, size_t { 4096 });
          continue;
        }
        lua_pushnil(state);
        return 1;
      }

      const auto count = (std::min)(result.value(), bodies.size());
      lua_createtable(state, static_cast<int>(count), 0);
      for (size_t idx = 0; idx < count; ++idx) {
        PushBodyId(state, bodies[idx]);
        lua_rawseti(state, -2, static_cast<int>(idx + 1));
      }
      return 1;
    }

    lua_pushnil(state);
    return 1;
  }

  auto LuaArticulationGetAuthority(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_authority")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->Articulations().GetAuthority(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto text = physics::aggregate::to_string(result.value());
    lua_pushlstring(state, text.data(), text.size());
    return 1;
  }

  auto LuaArticulationFlushStructuralChanges(lua_State* state) -> int
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
      = physics_module->Articulations().FlushStructuralChanges(*world_id_opt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(result.value()));
    return 1;
  }

} // namespace

auto RegisterPhysicsArticulationBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto articulation_index
    = PushOxygenSubtable(state, physics_index, "articulation");

  lua_pushcfunction(
    state, LuaArticulationCreate, "physics.articulation.create");
  lua_setfield(state, articulation_index, "create");
  lua_pushcfunction(
    state, LuaArticulationDestroy, "physics.articulation.destroy");
  lua_setfield(state, articulation_index, "destroy");
  lua_pushcfunction(
    state, LuaArticulationAddLink, "physics.articulation.add_link");
  lua_setfield(state, articulation_index, "add_link");
  lua_pushcfunction(
    state, LuaArticulationRemoveLink, "physics.articulation.remove_link");
  lua_setfield(state, articulation_index, "remove_link");
  lua_pushcfunction(
    state, LuaArticulationGetRootBody, "physics.articulation.get_root_body");
  lua_setfield(state, articulation_index, "get_root_body");
  lua_pushcfunction(state, LuaArticulationGetLinkBodies,
    "physics.articulation.get_link_bodies");
  lua_setfield(state, articulation_index, "get_link_bodies");
  lua_pushcfunction(
    state, LuaArticulationGetAuthority, "physics.articulation.get_authority");
  lua_setfield(state, articulation_index, "get_authority");
  lua_pushcfunction(state, LuaArticulationFlushStructuralChanges,
    "physics.articulation.flush_structural_changes");
  lua_setfield(state, articulation_index, "flush_structural_changes");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
