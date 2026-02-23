//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsCharacterBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto ParseCharacterDesc(lua_State* state, const int arg_index,
    physics::PhysicsModule& physics_module) -> physics::character::CharacterDesc
  {
    physics::character::CharacterDesc desc {};
    if (lua_isnoneornil(state, arg_index) != 0) {
      return desc;
    }
    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    lua_getfield(state, desc_index, "shape_id");
    if (lua_isnil(state, -1) == 0) {
      const auto* shape_id = CheckShapeId(state, -1);
      const auto shape_result
        = physics_module.Shapes().GetShapeDesc(shape_id->shape_id);
      if (!shape_result.has_value()) {
        lua_pop(state, 1);
        luaL_error(state, "invalid shape_id in character descriptor");
      }
      desc.shape = shape_result.value().geometry;
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "shape");
    if (lua_isnil(state, -1) == 0) {
      desc.shape = ParseCollisionShape(state, -1);
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "mass_kg");
    if (lua_isnil(state, -1) == 0) {
      desc.mass_kg = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "max_slope_angle");
    if (lua_isnil(state, -1) == 0) {
      desc.max_slope_angle_radians
        = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "max_strength");
    if (lua_isnil(state, -1) == 0) {
      desc.max_strength = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "character_padding");
    if (lua_isnil(state, -1) == 0) {
      desc.character_padding = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "penetration_recovery_speed");
    if (lua_isnil(state, -1) == 0) {
      desc.penetration_recovery_speed
        = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "predictive_contact_distance");
    if (lua_isnil(state, -1) == 0) {
      desc.predictive_contact_distance
        = static_cast<float>(luaL_checknumber(state, -1));
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "collision_layer");
    if (lua_isnil(state, -1) == 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw < 0) {
        luaL_error(state, "collision_layer must be non-negative");
      }
      desc.collision_layer = physics::CollisionLayer {
        static_cast<uint32_t>(raw),
      };
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "collision_mask");
    if (lua_isnil(state, -1) == 0) {
      const auto raw = luaL_checkinteger(state, -1);
      if (raw < 0) {
        luaL_error(state, "collision_mask must be non-negative");
      }
      desc.collision_mask = physics::CollisionMask {
        static_cast<uint32_t>(raw),
      };
    }
    lua_pop(state, 1);

    if (!std::isfinite(desc.mass_kg)
      || !std::isfinite(desc.max_slope_angle_radians)
      || !std::isfinite(desc.max_strength)
      || !std::isfinite(desc.character_padding)
      || !std::isfinite(desc.penetration_recovery_speed)
      || !std::isfinite(desc.predictive_contact_distance)) {
      luaL_error(state, "character descriptor numeric fields must be finite");
    }

    return desc;
  }

  auto RejectCommandOutsideGameplay(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsCommandAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.character.{} rejected outside gameplay phase "
      "(active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    return true;
  }

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.character.{} rejected during fixed_simulation",
      op_name);
    lua_pushnil(state);
    return true;
  }

  auto LuaCharacterAttach(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "attach")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.character.attach rejected outside gameplay/scene_mutation "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushnil(state);
      return 1;
    }

    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    auto* node = CheckSceneNode(state, 1);
    const auto desc = ParseCharacterDesc(state, 2, *physics_module);

    const auto character = physics::ScenePhysics::AttachCharacter(
      observer_ptr<physics::PhysicsModule> { physics_module }, *node, desc);
    if (!character.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushCharacterHandle(
      state, *world_id_opt, character->GetCharacterId());
  }

  auto LuaCharacterGet(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get")) {
      return 1;
    }
    auto* node = CheckSceneNode(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    const auto world_id_opt = GetPhysicsWorldId(state);
    if (physics_module == nullptr || !world_id_opt.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto character = physics::ScenePhysics::GetCharacter(
      observer_ptr<physics::PhysicsModule> { physics_module },
      node->GetHandle());
    if (!character.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    return PushCharacterHandle(
      state, *world_id_opt, character->GetCharacterId());
  }

  auto LuaCharacterHandleGetId(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_id")) {
      return 1;
    }
    const auto* handle = CheckCharacterHandle(state, 1);
    return PushCharacterId(state, handle->character_id);
  }

  auto LuaCharacterHandleMove(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "move")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "move")) {
      lua_pushnil(state);
      return 1;
    }

    const auto* handle = CheckCharacterHandle(state, 1);
    const auto velocity
      = CheckVec3(state, 2, "character:move expects velocity vector");

    // Signature: handle:move(velocity, jump_pressed?, dt)
    // jump_pressed is optional; detect by checking if arg 3 is boolean.
    bool jump_pressed = false;
    int dt_arg_index = 3;
    if (lua_gettop(state) >= 4 || lua_isboolean(state, 3) != 0) {
      jump_pressed = (lua_toboolean(state, 3) != 0);
      dt_arg_index = 4;
    }

    const auto dt = static_cast<float>(luaL_checknumber(state, dt_arg_index));
    if (!std::isfinite(dt) || dt <= 0.0F) {
      luaL_error(state, "character:move dt must be a positive finite number");
      return 0;
    }

    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    const physics::character::CharacterMoveInput input {
      .desired_velocity = velocity,
      .jump_pressed = jump_pressed,
    };
    const auto node_handle
      = physics_module->GetNodeForCharacterId(handle->character_id);
    if (!node_handle.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto character = physics::ScenePhysics::GetCharacter(
      observer_ptr<physics::PhysicsModule> { physics_module }, *node_handle);
    if (!character.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto result = character->Move(input, dt);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto& move_result = result.value();
    lua_createtable(state, 0, 5);
    lua_pushboolean(state, move_result.state.is_grounded ? 1 : 0);
    lua_setfield(state, -2, "is_grounded");
    PushVec3(state, move_result.state.position);
    lua_setfield(state, -2, "position");
    PushQuat(state, move_result.state.rotation.x, move_result.state.rotation.y,
      move_result.state.rotation.z, move_result.state.rotation.w);
    lua_setfield(state, -2, "rotation");
    PushVec3(state, move_result.state.velocity);
    lua_setfield(state, -2, "velocity");
    if (move_result.hit_body.has_value()) {
      PushBodyId(state, *move_result.hit_body);
    } else {
      lua_pushnil(state);
    }
    lua_setfield(state, -2, "hit_body");
    return 1;
  }

} // namespace

auto RegisterCharacterBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  RegisterPhysicsCharacterHandleMetatable(state);
  RegisterPhysicsCharacterIdMetatable(state);

  luaL_getmetatable(state, kPhysicsCharacterHandleMetatable);
  lua_pushcfunction(
    state, LuaCharacterHandleGetId, "physics.character_handle.get_id");
  lua_setfield(state, -2, "get_id");
  lua_pushcfunction(
    state, LuaCharacterHandleMove, "physics.character_handle.move");
  lua_setfield(state, -2, "move");
  lua_pop(state, 1);

  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto character_index
    = PushOxygenSubtable(state, physics_index, "character");

  lua_pushcfunction(state, LuaCharacterAttach, "physics.character.attach");
  lua_setfield(state, character_index, "attach");
  lua_pushcfunction(state, LuaCharacterGet, "physics.character.get");
  lua_setfield(state, character_index, "get");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
