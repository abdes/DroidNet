//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <optional>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/EventsBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBodyBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  auto ParseBodyType(lua_State* state, const int desc_index)
    -> physics::body::BodyType
  {
    lua_getfield(state, desc_index, "body_type");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      return physics::body::BodyType::kStatic;
    }
    const auto* body_type_text = luaL_checkstring(state, -1);
    const auto body_type
      = std::string_view(body_type_text == nullptr ? "" : body_type_text);
    lua_pop(state, 1);

    if (body_type == "static") {
      return physics::body::BodyType::kStatic;
    }
    if (body_type == "dynamic") {
      return physics::body::BodyType::kDynamic;
    }
    if (body_type == "kinematic") {
      return physics::body::BodyType::kKinematic;
    }
    luaL_error(state, "body_type must be 'static', 'dynamic', or 'kinematic'");
    return physics::body::BodyType::kStatic;
  }

  auto ParseBodyFlagsTable(lua_State* state, const int flags_table_index)
    -> physics::body::BodyFlags
  {
    physics::body::BodyFlags flags = physics::body::BodyFlags::kNone;
    const auto len = lua_objlen(state, flags_table_index);
    for (int i = 1; i <= len; ++i) {
      lua_rawgeti(state, flags_table_index, i);
      const auto* flag_text = luaL_checkstring(state, -1);
      const auto flag = std::string_view(flag_text == nullptr ? "" : flag_text);
      lua_pop(state, 1);

      if (flag == "none") {
        continue;
      }
      if (flag == "enable_gravity") {
        flags |= physics::body::BodyFlags::kEnableGravity;
        continue;
      }
      if (flag == "is_trigger") {
        flags |= physics::body::BodyFlags::kIsTrigger;
        continue;
      }
      if (flag == "enable_ccd") {
        flags |= physics::body::BodyFlags::kEnableContinuousCollisionDetection;
        continue;
      }
      luaL_error(state, "unknown body flag '%s'", flag_text);
      return physics::body::BodyFlags::kNone;
    }
    return flags;
  }

  auto ParseBodyFlags(lua_State* state, const int desc_index)
    -> physics::body::BodyFlags
  {
    lua_getfield(state, desc_index, "flags");
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      lua_getfield(state, desc_index, "body_flags");
    }
    if (lua_isnil(state, -1) != 0) {
      lua_pop(state, 1);
      return physics::body::BodyFlags::kEnableGravity;
    }
    luaL_checktype(state, -1, LUA_TTABLE);
    const auto flags = ParseBodyFlagsTable(state, lua_absindex(state, -1));
    lua_pop(state, 1);
    return flags;
  }

  auto ParseBodyDesc(lua_State* state, const int arg_index,
    physics::PhysicsModule& physics_module) -> physics::body::BodyDesc
  {
    physics::body::BodyDesc desc {};
    if (lua_isnoneornil(state, arg_index) != 0) {
      return desc;
    }

    const auto desc_index = lua_absindex(state, arg_index);
    luaL_checktype(state, desc_index, LUA_TTABLE);

    desc.type = ParseBodyType(state, desc_index);
    desc.flags = ParseBodyFlags(state, desc_index);

    lua_getfield(state, desc_index, "shape_id");
    if (lua_isnil(state, -1) == 0) {
      const auto* shape_id = CheckShapeId(state, -1);
      const auto shape_result
        = physics_module.Shapes().GetShapeDesc(shape_id->shape_id);
      if (!shape_result.has_value()) {
        lua_pop(state, 1);
        luaL_error(state, "invalid shape_id in body descriptor");
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
      if (!std::isfinite(desc.mass_kg)) {
        luaL_error(state, "mass_kg must be finite");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "linear_damping");
    if (lua_isnil(state, -1) == 0) {
      desc.linear_damping = static_cast<float>(luaL_checknumber(state, -1));
      if (!std::isfinite(desc.linear_damping)) {
        luaL_error(state, "linear_damping must be finite");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "angular_damping");
    if (lua_isnil(state, -1) == 0) {
      desc.angular_damping = static_cast<float>(luaL_checknumber(state, -1));
      if (!std::isfinite(desc.angular_damping)) {
        luaL_error(state, "angular_damping must be finite");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "gravity_factor");
    if (lua_isnil(state, -1) == 0) {
      desc.gravity_factor = static_cast<float>(luaL_checknumber(state, -1));
      if (!std::isfinite(desc.gravity_factor)) {
        luaL_error(state, "gravity_factor must be finite");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "friction");
    if (lua_isnil(state, -1) == 0) {
      desc.friction = static_cast<float>(luaL_checknumber(state, -1));
      if (!std::isfinite(desc.friction)) {
        luaL_error(state, "friction must be finite");
      }
    }
    lua_pop(state, 1);

    lua_getfield(state, desc_index, "restitution");
    if (lua_isnil(state, -1) == 0) {
      desc.restitution = static_cast<float>(luaL_checknumber(state, -1));
      if (!std::isfinite(desc.restitution)) {
        luaL_error(state, "restitution must be finite");
      }
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

    return desc;
  }

  auto ResolveBodyTypeForHandle(physics::PhysicsModule& module,
    const physics::BodyId body_id, const physics::body::BodyType fallback_type)
    -> physics::body::BodyType
  {
    const auto mapped_type = module.GetBodyTypeForBodyId(body_id);
    if (mapped_type.has_value()) {
      return *mapped_type;
    }
    LOG_F(WARNING, "physics.body could not resolve type for {}", body_id);
    return fallback_type;
  }

  auto RejectCommandOutsideGameplay(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsCommandAllowed(state)) {
      return false;
    }
    LOG_F(WARNING,
      "physics.body.{} rejected outside gameplay phase (active_phase='{}')",
      op_name, GetActiveEventPhase(state));
    return true;
  }

  auto RejectFixedSimulationWithNil(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.body.{} rejected during fixed_simulation", op_name);
    lua_pushnil(state);
    return true;
  }

  auto RejectFixedSimulationWithBool(lua_State* state, const char* op_name)
    -> bool
  {
    if (IsPhysicsScriptablePhase(state)) {
      return false;
    }
    LOG_F(WARNING, "physics.body.{} rejected during fixed_simulation", op_name);
    lua_pushboolean(state, 0);
    return true;
  }

  auto PushBodyResultOrNil(
    lua_State* state, const physics::PhysicsResult<Vec3>& result) -> int
  {
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, result.value());
  }

  auto LuaBodyAttach(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "attach")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.body.attach rejected outside gameplay/scene_mutation "
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

    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto desc = ParseBodyDesc(state, 2, *physics_module);

    const auto body = physics::ScenePhysics::AttachRigidBody(
      observer_ptr<physics::PhysicsModule> { physics_module }, *node, desc);
    if (!body.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto body_type
      = ResolveBodyTypeForHandle(*physics_module, body->GetBodyId(), desc.type);
    return PushBodyHandle(state, *world_id_opt, body->GetBodyId(), body_type);
  }

  auto LuaBodyGet(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get")) {
      return 1;
    }
    auto* node = TryCheckSceneNode(state, 1);
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

    const auto body = physics::ScenePhysics::GetRigidBody(
      observer_ptr<physics::PhysicsModule> { physics_module },
      node->GetHandle());
    if (!body.has_value()) {
      lua_pushnil(state);
      return 1;
    }

    const auto body_type = ResolveBodyTypeForHandle(
      *physics_module, body->GetBodyId(), physics::body::BodyType::kStatic);
    return PushBodyHandle(state, *world_id_opt, body->GetBodyId(), body_type);
  }

  auto LuaBodyHandleGetId(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_id")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    return PushBodyId(state, handle->body_id);
  }

  auto LuaBodyHandleGetBodyType(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_body_type")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    lua_pushstring(state, physics::body::to_string(handle->body_type));
    return 1;
  }

  auto LuaBodyHandleGetPosition(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_position")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushBodyResultOrNil(state,
      physics_module->Bodies().GetBodyPosition(
        handle->world_id, handle->body_id));
  }

  auto LuaBodyHandleGetRotation(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_rotation")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto result = physics_module->Bodies().GetBodyRotation(
      handle->world_id, handle->body_id);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto& q = result.value();
    return PushQuat(state, q.x, q.y, q.z, q.w);
  }

  auto LuaBodyHandleGetLinearVelocity(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_linear_velocity")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushBodyResultOrNil(state,
      physics_module->Bodies().GetLinearVelocity(
        handle->world_id, handle->body_id));
  }

  auto LuaBodyHandleGetAngularVelocity(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "get_angular_velocity")) {
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushBodyResultOrNil(state,
      physics_module->Bodies().GetAngularVelocity(
        handle->world_id, handle->body_id));
  }

  auto PushBoolResult(
    lua_State* state, const physics::PhysicsResult<void>& result) -> int
  {
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

  auto RejectStaticMutation(lua_State* state, const char* op_name,
    const PhysicsBodyHandleUserdata* handle) -> bool
  {
    if (handle->body_type != physics::body::BodyType::kStatic) {
      return false;
    }
    LOG_F(WARNING, "physics.body.{} rejected for static body {}", op_name,
      handle->body_id);
    lua_pushboolean(state, 0);
    return true;
  }

  auto LuaBodyHandleSetLinearVelocity(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "set_linear_velocity")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "set_linear_velocity")) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    if (RejectStaticMutation(state, "set_linear_velocity", handle)) {
      return 1;
    }
    Vec3 velocity {};
    if (!TryCheckVec3(state, 2, velocity)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    return PushBoolResult(state,
      physics_module->Bodies().SetLinearVelocity(
        handle->world_id, handle->body_id, velocity));
  }

  auto LuaBodyHandleSetAngularVelocity(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "set_angular_velocity")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "set_angular_velocity")) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    if (RejectStaticMutation(state, "set_angular_velocity", handle)) {
      return 1;
    }
    Vec3 velocity {};
    if (!TryCheckVec3(state, 2, velocity)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    return PushBoolResult(state,
      physics_module->Bodies().SetAngularVelocity(
        handle->world_id, handle->body_id, velocity));
  }

  auto LuaBodyHandleAddForce(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "add_force")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "add_force")) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    if (RejectStaticMutation(state, "add_force", handle)) {
      return 1;
    }
    Vec3 force {};
    if (!TryCheckVec3(state, 2, force)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    return PushBoolResult(state,
      physics_module->Bodies().AddForce(
        handle->world_id, handle->body_id, force));
  }

  auto LuaBodyHandleAddImpulse(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "add_impulse")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "add_impulse")) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    if (RejectStaticMutation(state, "add_impulse", handle)) {
      return 1;
    }
    Vec3 impulse {};
    if (!TryCheckVec3(state, 2, impulse)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    return PushBoolResult(state,
      physics_module->Bodies().AddImpulse(
        handle->world_id, handle->body_id, impulse));
  }

  auto LuaBodyHandleAddTorque(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "add_torque")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "add_torque")) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* handle = CheckBodyHandle(state, 1);
    if (RejectStaticMutation(state, "add_torque", handle)) {
      return 1;
    }
    Vec3 torque {};
    if (!TryCheckVec3(state, 2, torque)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    return PushBoolResult(state,
      physics_module->Bodies().AddTorque(
        handle->world_id, handle->body_id, torque));
  }

  auto LuaBodyHandleMoveKinematic(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "move_kinematic")) {
      return 1;
    }
    if (RejectCommandOutsideGameplay(state, "move_kinematic")) {
      lua_pushboolean(state, 0);
      return 1;
    }

    const auto* handle = CheckBodyHandle(state, 1);
    if (handle->body_type != physics::body::BodyType::kKinematic) {
      LOG_F(WARNING,
        "physics.body.move_kinematic rejected for non-kinematic {}",
        handle->body_id);
      lua_pushboolean(state, 0);
      return 1;
    }

    Vec3 target_pos {};
    if (!TryCheckVec3(state, 2, target_pos)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto* target_rot = TryCheckQuat(state, 3);
    if (target_rot == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto dt = static_cast<float>(luaL_checknumber(state, 4));
    if (!std::isfinite(dt) || dt <= 0.0F) {
      luaL_error(state, "move_kinematic dt must be a positive finite number");
      return 0;
    }
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }

    return PushBoolResult(state,
      physics_module->Bodies().MoveKinematic(handle->world_id, handle->body_id,
        target_pos,
        Quat { target_rot->w, target_rot->x, target_rot->y, target_rot->z },
        dt));
  }

  auto LuaBodyHandleAddShape(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithNil(state, "add_shape")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.body.add_shape rejected outside gameplay/scene_mutation "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushnil(state);
      return 1;
    }

    const auto* handle = CheckBodyHandle(state, 1);
    const auto* shape_id = CheckShapeId(state, 2);
    Vec3 local_position { 0.0F, 0.0F, 0.0F };
    Quat local_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    if (lua_isnoneornil(state, 3) == 0) {
      if (!TryCheckVec3(state, 3, local_position)) {
        lua_pushnil(state);
        return 1;
      }
    }
    if (lua_isnoneornil(state, 4) == 0) {
      const auto* q = TryCheckQuat(state, 4);
      if (q == nullptr) {
        lua_pushnil(state);
        return 1;
      }
      local_rotation = Quat { q->w, q->x, q->y, q->z };
    }

    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushnil(state);
      return 1;
    }

    const auto result = physics_module->Bodies().AddBodyShape(handle->world_id,
      handle->body_id, shape_id->shape_id, local_position, local_rotation);
    if (!result.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushShapeInstanceId(state, result.value());
  }

  auto LuaBodyHandleRemoveShape(lua_State* state) -> int
  {
    if (RejectFixedSimulationWithBool(state, "remove_shape")) {
      return 1;
    }
    if (!IsAttachAllowed(state)) {
      LOG_F(WARNING,
        "physics.body.remove_shape rejected outside gameplay/scene_mutation "
        "(active_phase='{}')",
        GetActiveEventPhase(state));
      lua_pushboolean(state, 0);
      return 1;
    }

    const auto* handle = CheckBodyHandle(state, 1);
    const auto* shape_instance = CheckShapeInstanceId(state, 2);
    auto* physics_module = GetPhysicsModule(state);
    if (physics_module == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }

    const auto result = physics_module->Bodies().RemoveBodyShape(
      handle->world_id, handle->body_id, shape_instance->shape_instance_id);
    lua_pushboolean(state, result.has_value() ? 1 : 0);
    return 1;
  }

} // namespace

auto RegisterBodyBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  RegisterPhysicsBodyHandleMetatable(state);
  RegisterPhysicsBodyIdMetatable(state);
  RegisterPhysicsShapeInstanceIdMetatable(state);

  luaL_getmetatable(state, kPhysicsBodyHandleMetatable);
  lua_pushcfunction(state, LuaBodyHandleGetId, "physics.body_handle.get_id");
  lua_setfield(state, -2, "get_id");
  lua_pushcfunction(
    state, LuaBodyHandleGetBodyType, "physics.body_handle.get_body_type");
  lua_setfield(state, -2, "get_body_type");
  lua_pushcfunction(
    state, LuaBodyHandleGetPosition, "physics.body_handle.get_position");
  lua_setfield(state, -2, "get_position");
  lua_pushcfunction(
    state, LuaBodyHandleGetRotation, "physics.body_handle.get_rotation");
  lua_setfield(state, -2, "get_rotation");
  lua_pushcfunction(state, LuaBodyHandleGetLinearVelocity,
    "physics.body_handle.get_linear_velocity");
  lua_setfield(state, -2, "get_linear_velocity");
  lua_pushcfunction(state, LuaBodyHandleGetAngularVelocity,
    "physics.body_handle.get_angular_velocity");
  lua_setfield(state, -2, "get_angular_velocity");
  lua_pushcfunction(state, LuaBodyHandleSetLinearVelocity,
    "physics.body_handle.set_linear_velocity");
  lua_setfield(state, -2, "set_linear_velocity");
  lua_pushcfunction(state, LuaBodyHandleSetAngularVelocity,
    "physics.body_handle.set_angular_velocity");
  lua_setfield(state, -2, "set_angular_velocity");
  lua_pushcfunction(
    state, LuaBodyHandleAddForce, "physics.body_handle.add_force");
  lua_setfield(state, -2, "add_force");
  lua_pushcfunction(
    state, LuaBodyHandleAddImpulse, "physics.body_handle.add_impulse");
  lua_setfield(state, -2, "add_impulse");
  lua_pushcfunction(
    state, LuaBodyHandleAddTorque, "physics.body_handle.add_torque");
  lua_setfield(state, -2, "add_torque");
  lua_pushcfunction(
    state, LuaBodyHandleMoveKinematic, "physics.body_handle.move_kinematic");
  lua_setfield(state, -2, "move_kinematic");
  lua_pushcfunction(
    state, LuaBodyHandleAddShape, "physics.body_handle.add_shape");
  lua_setfield(state, -2, "add_shape");
  lua_pushcfunction(
    state, LuaBodyHandleRemoveShape, "physics.body_handle.remove_shape");
  lua_setfield(state, -2, "remove_shape");
  lua_pop(state, 1);

  const auto physics_index
    = PushOxygenSubtable(state, oxygen_table_index, "physics");
  const auto body_index = PushOxygenSubtable(state, physics_index, "body");

  lua_pushcfunction(state, LuaBodyAttach, "physics.body.attach");
  lua_setfield(state, body_index, "attach");
  lua_pushcfunction(state, LuaBodyGet, "physics.body.get");
  lua_setfield(state, body_index, "get");

  lua_pop(state, 2);
}

} // namespace oxygen::scripting::bindings
