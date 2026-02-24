//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <string_view>
#include <utility>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsJointBindings.h>
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
      WARNING, "physics.joint.{} rejected during fixed_simulation", op_name);
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
      WARNING, "physics.joint.{} rejected during fixed_simulation", op_name);
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
      "physics.joint.{} rejected outside gameplay phase (active_phase='{}')",
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
      "physics.joint.{} rejected outside gameplay phase (active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    lua_pushboolean(state, 0);
    return true;
  }

  auto ReadFiniteField(lua_State* state, const int table_index,
    const char* field_name, const float fallback) -> float
  {
    lua_getfield(state, table_index, field_name);
    float value = fallback;
    if (lua_isnil(state, -1) == 0) {
      value = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);
    if (!std::isfinite(value)) {
      luaL_error(state, "joint field '%s' must be finite", field_name);
      return fallback;
    }
    return value;
  }

  auto ParseJointType(lua_State* state, const int table_index)
    -> physics::joint::JointType
  {
    lua_getfield(state, table_index, "type");
    const auto* text = luaL_checkstring(state, -1);
    const auto view = std::string_view(text == nullptr ? "" : text);
    lua_pop(state, 1);
    if (view == "fixed") {
      return physics::joint::JointType::kFixed;
    }
    if (view == "distance") {
      return physics::joint::JointType::kDistance;
    }
    if (view == "hinge") {
      return physics::joint::JointType::kHinge;
    }
    if (view == "slider") {
      return physics::joint::JointType::kSlider;
    }
    if (view == "spherical") {
      return physics::joint::JointType::kSpherical;
    }

    luaL_error(state,
      "joint.type must be 'fixed'|'distance'|'hinge'|'slider'|'spherical'");
    return physics::joint::JointType::kFixed;
  }

  auto ParseJointDesc(lua_State* state, const int arg_index)
    -> physics::joint::JointDesc
  {
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    physics::joint::JointDesc desc {};
    desc.type = ParseJointType(state, desc_index);

    lua_getfield(state, desc_index, "body_a_id");
    desc.body_a = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "body_b_id");
    desc.body_b = ParseBodyIdOrHandle(state, -1);
    lua_pop(state, 1);
    if (desc.body_a == desc.body_b) {
      luaL_error(state, "joint.body_b_id must differ from joint.body_a_id");
      return {};
    }

    lua_getfield(state, desc_index, "anchor_a");
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, desc.anchor_a)) {
        luaL_error(state, "joint.anchor_a must be a vec3");
        return {};
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "anchor_b");
    if (lua_isnil(state, -1) == 0) {
      if (!TryCheckVec3(state, -1, desc.anchor_b)) {
        luaL_error(state, "joint.anchor_b must be a vec3");
        return {};
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "collide_connected");
    if (lua_isnil(state, -1) == 0) {
      luaL_checktype(state, -1, LUA_TBOOLEAN);
      desc.collide_connected = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);

    desc.stiffness = ReadFiniteField(state, desc_index, "stiffness", 0.0F);
    desc.damping = ReadFiniteField(state, desc_index, "damping", 0.0F);
    return desc;
  }

  auto ParseJointIdOrHandle(lua_State* state, const int index)
    -> std::pair<physics::WorldId, physics::JointId>
  {
    if (lua_isuserdata(state, index) == 0) {
      luaL_argerror(state, index, "expected JointHandle or JointId userdata");
      return { physics::kInvalidWorldId, physics::kInvalidJointId };
    }

    if (lua_getmetatable(state, index) == 0) {
      luaL_argerror(state, index, "expected JointHandle or JointId userdata");
      return { physics::kInvalidWorldId, physics::kInvalidJointId };
    }
    const int mt_index = lua_gettop(state);

    luaL_getmetatable(state, kPhysicsJointHandleMetatable);
    const bool is_joint_handle = lua_rawequal(state, mt_index, -1) != 0;
    lua_pop(state, 1);
    if (is_joint_handle) {
      const auto* handle = CheckJointHandle(state, index);
      lua_pop(state, 1);
      return { handle->world_id, handle->joint_id };
    }

    luaL_getmetatable(state, kPhysicsJointIdMetatable);
    const bool is_joint_id = lua_rawequal(state, mt_index, -1) != 0;
    lua_pop(state, 1);
    if (is_joint_id) {
      const auto* id = CheckJointId(state, index);
      lua_pop(state, 1);
      const auto world_id_opt = GetPhysicsWorldId(state);
      if (!world_id_opt.has_value()) {
        return { physics::kInvalidWorldId, id->joint_id };
      }
      return { *world_id_opt, id->joint_id };
    }

    lua_pop(state, 1);
    luaL_argerror(state, index, "expected JointHandle or JointId userdata");
    return { physics::kInvalidWorldId, physics::kInvalidJointId };
  }

  auto LuaJointCreate(lua_State* state) -> int
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
    const auto desc = ParseJointDesc(state, 1);
    const auto result
      = physics_module->Joints().CreateJoint(*world_id_opt, desc);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushJointHandle(state, *world_id_opt, result.value());
  }

  auto LuaJointDestroy(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "destroy")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "destroy")) {
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    const auto [world_id, joint_id] = ParseJointIdOrHandle(state, 1);
    if (physics_module == nullptr || !physics::IsValid(world_id)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result
      = physics_module->Joints().DestroyJoint(world_id, joint_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto LuaJointSetEnabled(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "set_enabled")) {
      return 1;
    }
    if (RejectMutationOutsideGameplayWithBool(state, "set_enabled")) {
      return 1;
    }
    const auto [world_id, joint_id] = ParseJointIdOrHandle(state, 1);
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool enabled = lua_toboolean(state, 2) != 0;
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr || !physics::IsValid(world_id)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto result
      = physics_module->Joints().SetJointEnabled(world_id, joint_id, enabled);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

} // namespace

auto RegisterPhysicsJointBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  RegisterPhysicsJointHandleMetatable(state);
  RegisterPhysicsJointIdMetatable(state);

  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto joint_index = PushOxygenSubtable(state, physics_index, "joint");

  lua_pushcfunction(state, LuaJointCreate, "physics.joint.create");
  lua_setfield(state, joint_index, "create");
  lua_pushcfunction(state, LuaJointDestroy, "physics.joint.destroy");
  lua_setfield(state, joint_index, "destroy");
  lua_pushcfunction(state, LuaJointSetEnabled, "physics.joint.set_enabled");
  lua_setfield(state, joint_index, "set_enabled");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
