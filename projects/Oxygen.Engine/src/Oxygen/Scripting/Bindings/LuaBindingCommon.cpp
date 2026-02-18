//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/quaternion.hpp>
#include <lua.h>

#include <string>
#include <type_traits>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr const char* kBindingContextFieldName = "__oxgn_binding_context";
  thread_local observer_ptr<engine::FrameContext> g_active_frame_context {};

  auto NormalizeStackIndex(lua_State* state, const int index) -> int
  {
    if (index > 0) {
      return index;
    }
    const int top = lua_gettop(state);
    return top + index + 1;
  }

  auto LuaScriptContextGetParam(lua_State* state) -> int
  {
    return GetParamValue(state);
  }

  auto LuaScriptContextSetLocalRotationEuler(lua_State* state) -> int
  {
    if (!lua_isnumber(state, 2) || !lua_isnumber(state, 3)
      || !lua_isnumber(state, 4)) {
      return 0;
    }
    const float x = static_cast<float>(lua_tonumber(state, 2));
    const float y = static_cast<float>(lua_tonumber(state, 3));
    const float z = static_cast<float>(lua_tonumber(state, 4));
    return SetLocalRotationEuler(state, x, y, z);
  }

  auto LuaScriptContextSetLocalRotationQuat(lua_State* state) -> int
  {
    if (!lua_isnumber(state, 2) || !lua_isnumber(state, 3)
      || !lua_isnumber(state, 4) || !lua_isnumber(state, 5)) {
      return 0;
    }
    const float x = static_cast<float>(lua_tonumber(state, 2));
    const float y = static_cast<float>(lua_tonumber(state, 3));
    const float z = static_cast<float>(lua_tonumber(state, 4));
    const float w = static_cast<float>(lua_tonumber(state, 5));
    return SetLocalRotationQuat(state, x, y, z, w);
  }

  auto LuaScriptContextGetDeltaSeconds(lua_State* state) -> int
  {
    const auto* const context = GetBindingContextFromScriptArg(state);
    if (context == nullptr) {
      lua_pushnumber(state, 0.0F);
      return 1;
    }
    lua_pushnumber(state, context->dt_seconds);
    return 1;
  }
} // namespace

auto PushScriptContext(lua_State* state, LuaSlotExecutionContext* slot_context,
  const float dt_seconds) -> void
{
  lua_newtable(state);

  auto* const binding_context = static_cast<LuaBindingContext*>(
    lua_newuserdata(state, sizeof(LuaBindingContext)));
  binding_context->slot_context = slot_context;
  binding_context->dt_seconds = dt_seconds;
  lua_setfield(state, -2, kBindingContextFieldName);

  lua_pushcfunction(state, LuaScriptContextGetParam, "GetParam");
  lua_setfield(state, -2, "GetParam");

  lua_pushcfunction(
    state, LuaScriptContextSetLocalRotationEuler, "SetLocalRotationEuler");
  lua_setfield(state, -2, "SetLocalRotationEuler");

  lua_pushcfunction(
    state, LuaScriptContextSetLocalRotationQuat, "SetLocalRotation");
  lua_setfield(state, -2, "SetLocalRotation");

  lua_pushcfunction(state, LuaScriptContextGetDeltaSeconds, "GetDeltaSeconds");
  lua_setfield(state, -2, "GetDeltaSeconds");
}

auto SetActiveFrameContext(
  const observer_ptr<engine::FrameContext> frame_context) noexcept -> void
{
  g_active_frame_context = frame_context;
}

auto GetActiveFrameContext() noexcept -> observer_ptr<engine::FrameContext>
{
  return g_active_frame_context;
}

auto GetBindingContextFromScriptArg(
  lua_State* state, const int arg_index) noexcept -> LuaBindingContext*
{
  if (!lua_istable(state, arg_index)) {
    return nullptr;
  }
  lua_getfield(state, arg_index, kBindingContextFieldName);
  auto* const binding_context
    = static_cast<LuaBindingContext*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return binding_context;
}

auto PushScriptParam(lua_State* state, const data::ScriptParam& param) -> int
{
  std::visit(
    [state](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, bool>) {
        lua_pushboolean(state, value ? 1 : 0);
      } else if constexpr (std::is_same_v<T, int32_t>) {
        lua_pushinteger(state, value);
      } else if constexpr (std::is_same_v<T, float>) {
        lua_pushnumber(state, value);
      } else if constexpr (std::is_same_v<T, std::string>) {
        lua_pushlstring(state, value.c_str(), value.size());
      } else if constexpr (std::is_same_v<T, Vec2>) {
        lua_newtable(state);
        lua_pushnumber(state, value.x);
        lua_rawseti(state, -2, 1);
        lua_pushnumber(state, value.y);
        lua_rawseti(state, -2, 2);
      } else if constexpr (std::is_same_v<T, Vec3>) {
        lua_newtable(state);
        lua_pushnumber(state, value.x);
        lua_rawseti(state, -2, 1);
        lua_pushnumber(state, value.y);
        lua_rawseti(state, -2, 2);
        lua_pushnumber(state, value.z);
        lua_rawseti(state, -2, 3);
      } else if constexpr (std::is_same_v<T, Vec4>) {
        lua_newtable(state);
        lua_pushnumber(state, value.x);
        lua_rawseti(state, -2, 1);
        lua_pushnumber(state, value.y);
        lua_rawseti(state, -2, 2);
        lua_pushnumber(state, value.z);
        lua_rawseti(state, -2, 3);
        lua_pushnumber(state, value.w);
        lua_rawseti(state, -2, 4);
      } else {
        lua_pushnil(state);
      }
    },
    param);

  return 1;
}

auto GetParamValue(lua_State* state) -> int
{
  const auto* const context = GetBindingContextFromScriptArg(state);
  if (context == nullptr || context->slot_context == nullptr
    || context->slot_context->slot == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto* const key = lua_tostring(state, 2);
  if (key == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  for (const auto entry : context->slot_context->slot->Parameters()) {
    if (entry.key == key) {
      return PushScriptParam(state, entry.value.get());
    }
  }

  lua_pushnil(state);
  return 1;
}

auto SetLocalRotationEuler(
  lua_State* state, const float x, const float y, const float z) -> int
{
  const auto* const context = GetBindingContextFromScriptArg(state);
  if (context == nullptr || context->slot_context == nullptr
    || !context->slot_context->node.IsAlive()) {
    return 0;
  }

  auto transform = context->slot_context->node.GetTransform();
  const Quat rotation = glm::quat(Vec3 { x, y, z });
  (void)transform.SetLocalRotation(rotation);
  return 0;
}

auto SetLocalRotationQuat(lua_State* state, const float x, const float y,
  const float z, const float w) -> int
{
  const auto* const context = GetBindingContextFromScriptArg(state);
  if (context == nullptr || context->slot_context == nullptr
    || !context->slot_context->node.IsAlive()) {
    return 0;
  }

  auto transform = context->slot_context->node.GetTransform();
  (void)transform.SetLocalRotation(Quat { w, x, y, z });
  return 0;
}

auto PushOxygenSubtable(
  lua_State* state, const int oxygen_table_index, const char* field_name) -> int
{
  const int oxygen_index = NormalizeStackIndex(state, oxygen_table_index);
  lua_getfield(state, oxygen_index, field_name);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, oxygen_index, field_name);
  }
  return lua_gettop(state);
}

} // namespace oxygen::scripting::bindings
