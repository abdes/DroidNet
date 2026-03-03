//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
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

  auto IsKnownVehicleInputField(const std::string_view field_name) -> bool
  {
    return field_name == "forward" || field_name == "brake"
      || field_name == "steering" || field_name == "hand_brake";
  }

  auto ValidateVehicleInputKeys(lua_State* state, const int table_index) -> void
  {
    lua_pushnil(state);
    while (lua_next(state, table_index) != 0) {
      if (lua_type(state, -2) != LUA_TSTRING) {
        lua_pop(state, 1);
        luaL_error(
          state, "vehicle input table keys must be strings for known fields");
        return;
      }

      const auto* field_name = lua_tostring(state, -2);
      const auto field_view
        = std::string_view(field_name == nullptr ? "" : field_name);
      if (!IsKnownVehicleInputField(field_view)) {
        lua_pop(state, 1);
        luaL_error(state, "unknown vehicle input field '%s'",
          field_name == nullptr ? "<null>" : field_name);
        return;
      }

      lua_pop(state, 1);
    }
  }

  auto ParseVehicleWheelSide(lua_State* state, const int wheel_index,
    const int side_stack_index) -> physics::vehicle::VehicleWheelSide
  {
    const auto* side_text = luaL_checkstring(state, side_stack_index);
    const auto side = std::string_view(side_text == nullptr ? "" : side_text);
    if (side == "left") {
      return physics::vehicle::VehicleWheelSide::kLeft;
    }
    if (side == "right") {
      return physics::vehicle::VehicleWheelSide::kRight;
    }
    luaL_error(
      state, "vehicle.wheels[%d].side must be 'left'|'right'", wheel_index + 1);
    return physics::vehicle::VehicleWheelSide::kLeft;
  }

  auto ParseVehicleDesc(lua_State* state, const int arg_index,
    std::vector<physics::vehicle::VehicleWheelDesc>& wheel_descs_storage,
    std::vector<uint8_t>& constraint_blob_storage)
    -> physics::vehicle::VehicleDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "chassis_body_id");
    const auto chassis_id = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "wheels");
    const auto wheels_index = lua_absindex(state, -1);
    luaL_checktype(state, wheels_index, LUA_TTABLE);
    const auto wheel_count
      = static_cast<size_t>(lua_objlen(state, wheels_index));
    if (wheel_count < 2U) {
      luaL_error(state, "vehicle.wheels must contain at least two wheels");
      return {};
    }

    wheel_descs_storage.clear();
    wheel_descs_storage.reserve(wheel_count);
    for (size_t i = 0; i < wheel_count; ++i) {
      lua_rawgeti(state, wheels_index, static_cast<int>(i + 1U));
      const auto wheel_index = lua_absindex(state, -1);
      luaL_checktype(state, wheel_index, LUA_TTABLE);

      lua_getfield(state, wheel_index, "body_id");
      const auto body_id = ParseBodyIdOrHandle(state, -1);
      lua_pop(state, 1);

      lua_getfield(state, wheel_index, "axle_index");
      const auto axle_index_raw = luaL_checkinteger(state, -1);
      lua_pop(state, 1);
      if (axle_index_raw < 0
        || axle_index_raw
          > static_cast<lua_Integer>(std::numeric_limits<uint16_t>::max())) {
        luaL_error(
          state, "vehicle.wheels[%d].axle_index must be in [0, 65535]", i + 1);
        return {};
      }

      lua_getfield(state, wheel_index, "side");
      const auto side = ParseVehicleWheelSide(state, static_cast<int>(i), -1);
      lua_pop(state, 1);

      wheel_descs_storage.push_back(physics::vehicle::VehicleWheelDesc {
        .body_id = body_id,
        .axle_index = static_cast<uint16_t>(axle_index_raw),
        .side = side,
      });
      lua_pop(state, 1);
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "constraint_settings_blob");
    const auto blob_stack_index = lua_absindex(state, -1);
    size_t blob_size = 0U;
    const auto* blob_data
      = luaL_checklstring(state, blob_stack_index, &blob_size);
    constraint_blob_storage.assign(reinterpret_cast<const uint8_t*>(blob_data),
      reinterpret_cast<const uint8_t*>(blob_data) + blob_size);
    lua_pop(state, 1);
    if (constraint_blob_storage.empty()) {
      luaL_error(state, "vehicle.constraint_settings_blob must not be empty");
      return {};
    }

    return physics::vehicle::VehicleDesc {
      .chassis_body_id = chassis_id,
      .wheels = std::span<
        const physics::vehicle::VehicleWheelDesc> { wheel_descs_storage },
      .constraint_settings_blob
      = std::span<const uint8_t> { constraint_blob_storage },
    };
  }

  auto ParseVehicleControlInput(lua_State* state, const int arg_index)
    -> physics::vehicle::VehicleControlInput
  {
    const auto input_index = lua_absindex(state, arg_index);
    luaL_checktype(state, input_index, LUA_TTABLE);
    ValidateVehicleInputKeys(state, input_index);
    return physics::vehicle::VehicleControlInput {
      .forward
      = ParseClampedFinite(state, input_index, "forward", -1.0F, 1.0F, 0.0F),
      .brake
      = ParseClampedFinite(state, input_index, "brake", 0.0F, 1.0F, 0.0F),
      .steering
      = ParseClampedFinite(state, input_index, "steering", -1.0F, 1.0F, 0.0F),
      .hand_brake
      = ParseClampedFinite(state, input_index, "hand_brake", 0.0F, 1.0F, 0.0F),
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
    std::vector<physics::vehicle::VehicleWheelDesc> wheel_descs_storage {};
    std::vector<uint8_t> constraint_blob_storage {};
    const auto desc = ParseVehicleDesc(
      state, 1, wheel_descs_storage, constraint_blob_storage);

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

  auto PushVehicleHandleForNode(lua_State* state,
    physics::PhysicsModule& physics_module, const physics::WorldId world_id,
    const scene::NodeHandle node_handle) -> int
  {
    const auto aggregate_id = physics_module.GetAggregateIdForNode(node_handle);
    if (!physics::IsValid(aggregate_id)) {
      lua_pushnil(state);
      return 1;
    }

    const auto vehicle_state
      = physics_module.Vehicles().GetState(world_id, aggregate_id);
    if (!vehicle_state.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushAggregateHandle(state, world_id, aggregate_id);
  }

  auto LuaVehicleGetExact(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_exact")) {
      return 1;
    }

    const auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushVehicleHandleForNode(
      state, *physics_module, *world_id_opt, node->GetHandle());
  }

  auto LuaVehicleFindInAncestors(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "find_in_ancestors")) {
      return 1;
    }

    const auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr || !node->IsAlive()) {
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    auto current = std::optional<scene::SceneNode> { *node };
    while (current.has_value() && current->IsAlive()) {
      const auto pushed = PushVehicleHandleForNode(
        state, *physics_module, *world_id_opt, current->GetHandle());
      if (lua_isnil(state, -1) == 0) {
        return pushed;
      }
      lua_pop(state, 1);
      current = current->GetParent();
    }

    lua_pushnil(state);
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
    const auto input = ParseVehicleControlInput(state, 2);
    const auto* handle = CheckAggregateHandle(state, 1);
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
  lua_pushcfunction(state, LuaVehicleGetExact, "physics.vehicle.get_exact");
  lua_setfield(state, vehicle_index, "get_exact");
  lua_pushcfunction(
    state, LuaVehicleFindInAncestors, "physics.vehicle.find_in_ancestors");
  lua_setfield(state, vehicle_index, "find_in_ancestors");
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
