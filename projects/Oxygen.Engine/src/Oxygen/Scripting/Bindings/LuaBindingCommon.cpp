//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtc/quaternion.hpp>
#include <lua.h>
#include <lualib.h>

#include <string>
#include <type_traits>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArg1 = 1;
  constexpr int kLuaArg2 = 2;
  constexpr int kLuaArg3 = 3;
  constexpr int kLuaArg4 = 4;
  constexpr int kLuaArg5 = 5;
  constexpr int kStackCheckSize = 8;

  constexpr const char* kBindingContextFieldName = "__oxgn_binding_context";
  constexpr const char* kBindingContextMetatableName
    = "oxygen.scripting.binding_context";
  constexpr const char* kRuntimeContextFieldName = "__oxgn_runtime_context";
  constexpr const char* kRuntimeContextMetatableName
    = "oxygen.scripting.runtime_context";

  struct LuaRuntimeContext {
    observer_ptr<engine::FrameContext> frame_context;
    observer_ptr<AsyncEngine> engine;
  };

  auto LuaRuntimeContextGc(lua_State* state) -> int
  {
    auto* const runtime_context
      = static_cast<LuaRuntimeContext*>(lua_touserdata(state, 1));
    if (runtime_context != nullptr) {
      runtime_context->~LuaRuntimeContext();
    }
    return 0;
  }

  auto EnsureRuntimeContextMetatable(lua_State* state) -> void
  {
    const int status = luaL_newmetatable(state, kRuntimeContextMetatableName);
    if (status != 0) {
      lua_pushcfunction(state, LuaRuntimeContextGc, "runtime_context.__gc");
      lua_setfield(state, -2, "__gc");
    }
    lua_pop(state, 1);
  }

  auto FindRuntimeContext(lua_State* state) noexcept -> LuaRuntimeContext*
  {
    if (state == nullptr) {
      return nullptr;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kRuntimeContextFieldName);
    auto* runtime_context
      = static_cast<LuaRuntimeContext*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return runtime_context;
  }

  auto EnsureRuntimeContext(lua_State* state) -> LuaRuntimeContext*
  {
    if (state == nullptr) {
      return nullptr;
    }

    if (auto* existing = FindRuntimeContext(state); existing != nullptr) {
      return existing;
    }

    EnsureRuntimeContextMetatable(state);
    auto* const runtime_context_mem
      = lua_newuserdata(state, sizeof(LuaRuntimeContext));
    auto* runtime_context = new (runtime_context_mem) LuaRuntimeContext {};
    luaL_getmetatable(state, kRuntimeContextMetatableName);
    lua_setmetatable(state, -2);
    lua_setfield(state, LUA_REGISTRYINDEX, kRuntimeContextFieldName);
    return runtime_context;
  }

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
    if ((lua_isnumber(state, kLuaArg2) == 0)
      || (lua_isnumber(state, kLuaArg3) == 0)
      || (lua_isnumber(state, kLuaArg4) == 0)) {
      return 0;
    }
    const auto x = static_cast<float>(lua_tonumber(state, kLuaArg2));
    const auto y = static_cast<float>(lua_tonumber(state, kLuaArg3));
    const auto z = static_cast<float>(lua_tonumber(state, kLuaArg4));
    return SetLocalRotationEuler(state, x, y, z);
  }

  auto LuaScriptContextSetLocalRotationQuat(lua_State* state) -> int
  {
    if ((lua_isnumber(state, kLuaArg2) == 0)
      || (lua_isnumber(state, kLuaArg3) == 0)
      || (lua_isnumber(state, kLuaArg4) == 0)
      || (lua_isnumber(state, kLuaArg5) == 0)) {
      return 0;
    }
    const auto x = static_cast<float>(lua_tonumber(state, kLuaArg2));
    const auto y = static_cast<float>(lua_tonumber(state, kLuaArg3));
    const auto z = static_cast<float>(lua_tonumber(state, kLuaArg4));
    const auto w = static_cast<float>(lua_tonumber(state, kLuaArg5));
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

  auto EnsureBindingContextMetatable(lua_State* state) -> void
  {
    const int status = luaL_newmetatable(state, kBindingContextMetatableName);
    if (status != 0) {
      // No __gc needed as LuaBindingContext is trivially destructible
    }
    lua_pop(state, 1);
  }
} // namespace

auto PushScriptContext(lua_State* state, LuaSlotExecutionContext* slot_context,
  const float dt_seconds) -> void
{
  luaL_checkstack(state, kStackCheckSize, "script context creation");
  EnsureBindingContextMetatable(state);
  lua_newtable(state);

  auto* const binding_context_mem
    = lua_newuserdata(state, sizeof(LuaBindingContext));
  auto* const binding_context = new (binding_context_mem) LuaBindingContext {};
  if (slot_context != nullptr) {
    binding_context->slot_context = *slot_context;
    binding_context->has_slot_context = true;
  } else {
    binding_context->slot_context = {};
    binding_context->has_slot_context = false;
  }
  binding_context->dt_seconds = dt_seconds;
  luaL_getmetatable(state, kBindingContextMetatableName);
  lua_setmetatable(state, -2);
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

auto SetActiveFrameContext(lua_State* state,
  const observer_ptr<engine::FrameContext> frame_context) noexcept -> void
{
  if (auto* runtime_context = EnsureRuntimeContext(state);
    runtime_context != nullptr) {
    runtime_context->frame_context = frame_context;
  }
}

auto GetActiveFrameContext(lua_State* state) noexcept
  -> observer_ptr<engine::FrameContext>
{
  const auto* runtime_context = FindRuntimeContext(state);
  if (runtime_context == nullptr) {
    return {};
  }
  return runtime_context->frame_context;
}

auto SetActiveEngine(
  lua_State* state, const observer_ptr<AsyncEngine> engine) noexcept -> void
{
  if (auto* runtime_context = EnsureRuntimeContext(state);
    runtime_context != nullptr) {
    runtime_context->engine = engine;
  }
}

auto GetActiveEngine(lua_State* state) noexcept -> observer_ptr<AsyncEngine>
{
  const auto* runtime_context = FindRuntimeContext(state);
  if (runtime_context == nullptr) {
    return {};
  }
  return runtime_context->engine;
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
        lua_pushvector(state, value.x, value.y, 0.0F);
      } else if constexpr (std::is_same_v<T, Vec3>) {
        lua_pushvector(state, value.x, value.y, value.z);
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
  const auto* const context = GetBindingContextFromScriptArg(state, kLuaArg1);
  if (context == nullptr || !context->has_slot_context
    || context->slot_context.slot == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto* const key = lua_tostring(state, kLuaArg2);
  if (key == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  for (const auto entry : context->slot_context.slot->Parameters()) {
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
  if (context == nullptr || !context->has_slot_context) {
    return 0;
  }

  // Reconstruct SceneNode from handle
  const auto frame_context = GetActiveFrameContext(state);
  if (frame_context == nullptr) {
    return 0;
  }
  const auto& scene = frame_context->GetScene();
  auto node = scene->GetNode(context->slot_context.node_handle);
  if (!node || !node->IsAlive()) {
    return 0;
  }

  auto transform = node->GetTransform();
  const Quat rotation = glm::quat(Vec3 { x, y, z });
  (void)transform.SetLocalRotation(rotation);
  return 0;
}

auto SetLocalRotationQuat(lua_State* state, const float x, const float y,
  const float z, const float w) -> int
{
  const auto* const context = GetBindingContextFromScriptArg(state);
  if (context == nullptr || !context->has_slot_context) {
    return 0;
  }

  // Reconstruct SceneNode from handle
  const auto frame_context = GetActiveFrameContext(state);
  if (frame_context == nullptr) {
    return 0;
  }
  const auto& scene = frame_context->GetScene();
  auto node = scene->GetNode(context->slot_context.node_handle);
  if (!node || !node->IsAlive()) {
    return 0;
  }

  auto transform = node->GetTransform();
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
