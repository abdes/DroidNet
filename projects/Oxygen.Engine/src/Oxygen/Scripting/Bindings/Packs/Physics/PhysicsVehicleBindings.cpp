//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsVehicleBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(
      WARNING, "physics.vehicle.{} rejected during fixed_simulation", op_name);
    lua_pushnil(state);
    return true;
  }

  auto RejectFixedSimulationWithBool(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(
      WARNING, "physics.vehicle.{} rejected during fixed_simulation", op_name);
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
      "physics.vehicle.{} rejected outside gameplay phase (active_phase='{}')",
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
      "physics.vehicle.{} rejected outside gameplay phase (active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushboolean(state, 0);
    return true;
  }

  auto ParseClampedFinite(lua_State* state, const int table_index,
    const char* field_name, const float min_value, const float max_value,
    const float fallback) -> float
  {
    lua_getfield(state, table_index, field_name);
    float value = fallback;
    if (lua_isnil(state, -1) == 0) {
      value = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);
    if (!std::isfinite(value)) {
      luaL_error(state, "vehicle input field '%s' must be finite", field_name);
      return fallback;
    }
    return std::clamp(value, min_value, max_value);
  }

  auto ParseVehicleDesc(lua_State* state, const int arg_index,
    std::vector<physics::BodyId>& wheel_ids_storage)
    -> physics::vehicle::VehicleDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "chassis_body_id");
    const auto chassis_id = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "wheel_body_ids");
    wheel_ids_storage = ParseBodyIdOrHandleArray(state, -1);
    lua_pop(state, 1);

    return physics::vehicle::VehicleDesc {
      .chassis_body_id = chassis_id,
      .wheel_body_ids = std::span<const physics::BodyId> { wheel_ids_storage },
    };
  }

  auto ParseVehicleControlInput(lua_State* state, const int arg_index)
    -> physics::vehicle::VehicleControlInput
  {
    const auto input_index = lua_absindex(state, arg_index);
    luaL_checktype(state, input_index, LUA_TTABLE);
    return physics::vehicle::VehicleControlInput {
      .forward
      = ParseClampedFinite(state, input_index, "forward", -1.0F, 1.0F, 0.0F),
      .brake
      = ParseClampedFinite(state, input_index, "brake", 0.0F, 1.0F, 0.0F),
      .steering
      = ParseClampedFinite(state, input_index, "steering", -1.0F, 1.0F, 0.0F),
      .handbrake
      = ParseClampedFinite(state, input_index, "handbrake", 0.0F, 1.0F, 0.0F),
    };
  }

  auto LuaVehicleCreate(lua_State* state) -> int
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
    std::vector<physics::BodyId> wheel_ids_storage {};
    const auto desc = ParseVehicleDesc(state, 1, wheel_ids_storage);

    const auto result
      = physics_module->Vehicles().CreateVehicle(*world_id_opt, desc);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushAggregateHandle(state, *world_id_opt, result.value());
  }

  auto LuaVehicleDestroy(lua_State* state) -> int
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
    const auto result = physics_module->Vehicles().DestroyVehicle(
      handle->world_id, handle->aggregate_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaVehicleSetControlInput(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "set_control_input")) {
      return 1;
    }
    // set_control_input is a per-frame command (not a structural mutation),
    // so it uses IsCommandAllowed — same gate as body:set_linear_velocity.
    if (!IsCommandAllowed(state)) {
      LOG_F(WARNING,
        "physics.vehicle.set_control_input rejected outside gameplay phase "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    const auto input = ParseVehicleControlInput(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result = physics_module->Vehicles().SetControlInput(
      handle->world_id, handle->aggregate_id, input);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaVehicleGetState(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_state")) {
      return 1;
    }
    const auto* handle = CheckAggregateHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->Vehicles().GetState(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    lua_createtable(state, 0, 2);
    lua_pushnumber(state, result.value().forward_speed_mps);
    lua_setfield(state, -2, "forward_speed_mps");
    lua_pushboolean(state, result.value().grounded ? 1 : 0);
    lua_setfield(state, -2, "grounded");
    return 1;
  }

  auto LuaVehicleGetAuthority(lua_State* state) -> int
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
    const auto result = physics_module->Vehicles().GetAuthority(
      handle->world_id, handle->aggregate_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto text = physics::aggregate::to_string(result.value());
    lua_pushlstring(state, text.data(), text.size());
    return 1;
  }

  auto LuaVehicleFlushStructuralChanges(lua_State* state) -> int
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
      = physics_module->Vehicles().FlushStructuralChanges(*world_id_opt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(result.value()));
    return 1;
  }

} // namespace

auto RegisterPhysicsVehicleBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto vehicle_index
    = PushOxygenSubtable(state, physics_index, "vehicle");

  lua_pushcfunction(state, LuaVehicleCreate, "physics.vehicle.create");
  lua_setfield(state, vehicle_index, "create");
  lua_pushcfunction(state, LuaVehicleDestroy, "physics.vehicle.destroy");
  lua_setfield(state, vehicle_index, "destroy");
  lua_pushcfunction(
    state, LuaVehicleSetControlInput, "physics.vehicle.set_control_input");
  lua_setfield(state, vehicle_index, "set_control_input");
  lua_pushcfunction(state, LuaVehicleGetState, "physics.vehicle.get_state");
  lua_setfield(state, vehicle_index, "get_state");
  lua_pushcfunction(
    state, LuaVehicleGetAuthority, "physics.vehicle.get_authority");
  lua_setfield(state, vehicle_index, "get_authority");
  lua_pushcfunction(state, LuaVehicleFlushStructuralChanges,
    "physics.vehicle.flush_structural_changes");
  lua_setfield(state, vehicle_index, "flush_structural_changes");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
