//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr const char* kRuntimeContextFieldName = "__oxgn_runtime_context";
  constexpr const char* kRuntimeContextMetatableName
    = "oxygen.scripting.runtime_context";

  struct LuaRuntimeContext {
    observer_ptr<engine::FrameContext> frame_context;
    observer_ptr<IAsyncEngine> engine;
  };

  void LuaRuntimeContextDtor(lua_State* /*state*/, void* p)
  {
    static_cast<LuaRuntimeContext*>(p)->~LuaRuntimeContext();
  }

  auto EnsureRuntimeContextMetatable(lua_State* state) -> void
  {
    (void)luaL_newmetatable(state, kRuntimeContextMetatableName);
    lua_pop(state, 1);
  }

  auto FindRuntimeContext(lua_State* state) noexcept -> LuaRuntimeContext*
  {
    if (state == nullptr) {
      return nullptr;
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kRuntimeContextFieldName);
    if (lua_userdatatag(state, -1) != kTagRuntimeContext) {
      lua_pop(state, 1);
      return nullptr;
    }
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
    auto* const runtime_context_mem = lua_newuserdatatagged(
      state, sizeof(LuaRuntimeContext), kTagRuntimeContext);
    lua_setuserdatadtor(state, kTagRuntimeContext, LuaRuntimeContextDtor);

    auto* runtime_context
      = std::construct_at(static_cast<LuaRuntimeContext*>(runtime_context_mem));
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

} // namespace

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
  lua_State* state, const observer_ptr<IAsyncEngine> engine) noexcept -> void
{
  if (auto* runtime_context = EnsureRuntimeContext(state);
    runtime_context != nullptr) {
    runtime_context->engine = engine;
  }
}

auto GetActiveEngine(lua_State* state) noexcept -> observer_ptr<IAsyncEngine>
{
  const auto* runtime_context = FindRuntimeContext(state);
  if (runtime_context == nullptr) {
    return {};
  }
  return runtime_context->engine;
}

auto PushScriptParam(lua_State* state, const data::ScriptParam& param) -> int
{
  std::visit(
    [state](const auto& value) -> void {
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
      } else if constexpr (std::is_same_v<T, data::ScriptAssetParam>) {
        // Asset references reach scripts as the GUID text every
        // `oxygen.assets` load function accepts.
        if (value.key.IsNil()) {
          lua_pushnil(state);
        } else {
          const auto text = data::to_string(value.key);
          lua_pushlstring(state, text.c_str(), text.size());
        }
      } else {
        // An index alone names no node: script instances push the node the
        // slot's parameter is bound to (ScriptingComponent::BindNodeParameter).
        lua_pushnil(state);
      }
    },
    param);

  return 1;
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
