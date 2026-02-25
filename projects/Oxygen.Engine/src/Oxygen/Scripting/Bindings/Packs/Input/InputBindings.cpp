//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArg1 = 1;
  constexpr int kLuaArg2 = 2;
  constexpr int kLuaArg3 = 3;
  constexpr int kLuaArg4 = 4;
  constexpr int kLuaUpvalue1 = 1;
  constexpr int kLuaNoResults = 0;
  constexpr int kLuaSingleResult = 1;

  constexpr std::string_view kInputEventPrefix = "input.action.";
  constexpr std::array<std::string_view, 5> kSupportedEdges = {
    "triggered",
    "completed",
    "canceled",
    "released",
    "value_updated",
  };

  auto BuildActionEdgeEventName(
    std::string_view action_name, std::string_view edge_name) -> std::string
  {
    std::string event_name(kInputEventPrefix);
    event_name.append(action_name);
    event_name.push_back('.');
    event_name.append(edge_name);
    return event_name;
  }

  auto IsSupportedEdge(const std::string_view edge_name) -> bool
  {
    return std::ranges::any_of(kSupportedEdges,
      [edge_name](const std::string_view edge) { return edge == edge_name; });
  }

  auto RequireStringArg(lua_State* state, const int arg_index,
    const char* error_message) -> std::string_view
  {
    if (lua_type(state, arg_index) != LUA_TSTRING) {
      (void)luaL_error(state, "%s", error_message);
      return {};
    }
    const auto* value = lua_tostring(state, arg_index);
    const std::string_view value_sv
      = value == nullptr ? std::string_view {} : std::string_view(value);
    if (value_sv.empty()) {
      (void)luaL_error(state, "%s", error_message);
      return {};
    }
    return value_sv;
  }

  auto CallEventsRegistration(lua_State* state, std::string_view event_name)
    -> int
  {
    const bool has_nil_or_missing_options
      = lua_isnoneornil(state, kLuaArg4) != 0;
    const bool has_table_options = lua_istable(state, kLuaArg4) != 0;
    if (!has_nil_or_missing_options && !has_table_options) {
      (void)luaL_error(state,
        "oxygen.input action registration expects options table as arg #4 "
        "when provided");
      return kLuaNoResults;
    }

    if (lua_isfunction(state, lua_upvalueindex(kLuaUpvalue1)) == 0) {
      (void)luaL_error(state,
        "oxygen.input is not bound to a valid events registration function");
      return kLuaNoResults;
    }

    lua_pushvalue(state, lua_upvalueindex(kLuaUpvalue1));
    lua_pushlstring(state, event_name.data(), event_name.size());
    lua_pushvalue(state, kLuaArg3);
    if (has_table_options) {
      lua_pushvalue(state, kLuaArg4);
    } else {
      lua_pushnil(state);
    }

    if (lua_pcall(state, 3, kLuaSingleResult, 0) != LUA_OK) {
      const auto* message = lua_tostring(state, -1);
      const std::string message_str
        = message == nullptr ? "unknown error" : std::string(message);
      lua_pop(state, 1);
      (void)luaL_error(
        state, "oxygen.input registration failed: %s", message_str.c_str());
      return kLuaNoResults;
    }
    return kLuaSingleResult;
  }

  auto LuaInputEventName(lua_State* state) -> int
  {
    const auto action_name = RequireStringArg(
      state, kLuaArg1, "oxygen.input.event_name expects action name");
    const auto edge_name = RequireStringArg(
      state, kLuaArg2, "oxygen.input.event_name expects edge name");

    if (!IsSupportedEdge(edge_name)) {
      (void)luaL_error(state, "oxygen.input.event_name unsupported edge '%s'",
        std::string(edge_name).c_str());
      return kLuaNoResults;
    }

    const auto event_name = BuildActionEdgeEventName(action_name, edge_name);
    lua_pushlstring(state, event_name.data(), event_name.size());
    return kLuaSingleResult;
  }

  auto LuaInputOnActionImpl(lua_State* state) -> int
  {
    const auto action_name = RequireStringArg(
      state, kLuaArg1, "oxygen.input.on_action expects action name");
    const auto edge_name = RequireStringArg(
      state, kLuaArg2, "oxygen.input.on_action expects edge name");
    if (lua_isfunction(state, kLuaArg3) == 0) {
      (void)luaL_error(
        state, "oxygen.input.on_action expects callback as arg #3");
      return kLuaNoResults;
    }
    if (!IsSupportedEdge(edge_name)) {
      (void)luaL_error(state, "oxygen.input.on_action unsupported edge '%s'",
        std::string(edge_name).c_str());
      return kLuaNoResults;
    }

    const auto event_name = BuildActionEdgeEventName(action_name, edge_name);
    return CallEventsRegistration(state, event_name);
  }

  auto LuaInputOnAction(lua_State* state) -> int
  {
    return LuaInputOnActionImpl(state);
  }

  auto LuaInputOnceAction(lua_State* state) -> int
  {
    return LuaInputOnActionImpl(state);
  }
} // namespace

auto RegisterInputBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "input");

  lua_pushcfunction(state, LuaInputEventName, "input.event_name");
  lua_setfield(state, module_index, "event_name");

  lua_getfield(state, oxygen_table_index, "events");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 2); // events + input module
    (void)luaL_error(state, "oxygen.input requires oxygen.events namespace");
    return;
  }

  lua_getfield(state, -1, "on");
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 3); // on + events + input module
    (void)luaL_error(state, "oxygen.input requires oxygen.events.on");
    return;
  }
  lua_pushcclosure(state, LuaInputOnAction, "oxygen.input.on_action", 1);
  lua_setfield(state, module_index, "on_action");

  lua_getfield(state, -1, "once");
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 3); // once + events + input module
    (void)luaL_error(state, "oxygen.input requires oxygen.events.once");
    return;
  }
  lua_pushcclosure(state, LuaInputOnceAction, "oxygen.input.once_action", 1);
  lua_setfield(state, module_index, "once_action");

  lua_pop(state, 1); // events table

  lua_newtable(state);
  for (const auto edge_name : kSupportedEdges) {
    lua_pushlstring(state, edge_name.data(), edge_name.size());
    lua_setfield(state, -2, std::string(edge_name).c_str());
  }
  lua_setfield(state, module_index, "edges");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
