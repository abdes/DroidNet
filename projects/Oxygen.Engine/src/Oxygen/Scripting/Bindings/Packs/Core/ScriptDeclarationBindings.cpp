//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ScriptDeclarationBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  //! Registry key of the weak-keyed set of tables `oxygen.script` returned.
  constexpr const char* kScriptTablesKey = "oxygen.script.tables";

  constexpr auto kParamTypeNames = std::to_array<const char*>({
    "bool",
    "int",
    "float",
    "string",
    "vec2",
    "vec3",
    "vec4",
    "asset",
    "node",
  });

  auto PushScriptTables(lua_State* state) -> void
  {
    lua_getfield(state, LUA_REGISTRYINDEX, kScriptTablesKey);
    if (lua_istable(state, -1)) {
      return;
    }
    lua_pop(state, 1);
    lua_newtable(state); // set
    lua_newtable(state); // metatable
    lua_pushstring(state, "k");
    lua_setfield(state, -2, "__mode");
    lua_setmetatable(state, -2);
    lua_pushvalue(state, -1);
    lua_setfield(state, LUA_REGISTRYINDEX, kScriptTablesKey);
  }

  //! `oxygen.script(table)`: records and returns the script table.
  auto LuaScript(lua_State* state) -> int
  {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_settop(state, 1);
    PushScriptTables(state);
    lua_pushvalue(state, 1);
    lua_pushboolean(state, 1);
    lua_rawset(state, -3);
    lua_pop(state, 1);
    return 1;
  }

  //! `oxygen.param.<type>(table)`: returns the table tagged with its type,
  //! which is the closure's upvalue.
  auto LuaParam(lua_State* state) -> int
  {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_settop(state, 1);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_setfield(state, 1, "type");
    return 1;
  }

} // namespace

auto IsScriptTable(lua_State* state, const int index) -> bool
{
  if (!lua_istable(state, index)) {
    return false;
  }
  const int table_index = lua_absindex(state, index);
  PushScriptTables(state);
  lua_pushvalue(state, table_index);
  lua_rawget(state, -2);
  const bool recorded = lua_toboolean(state, -1) != 0;
  lua_pop(state, 2);
  return recorded;
}

auto RegisterScriptDeclarationBindings(
  lua_State* state, const int oxygen_table_index) -> void
{
  const int oxygen_index = lua_absindex(state, oxygen_table_index);
  lua_pushcfunction(state, LuaScript, "script");
  lua_setfield(state, oxygen_index, "script");

  const int param_index = PushOxygenSubtable(state, oxygen_index, "param");
  for (const auto* const type_name : kParamTypeNames) {
    const auto debug_name = std::string("param.") + type_name;
    lua_pushstring(state, type_name);
    lua_pushcclosure(state, LuaParam, debug_name.c_str(), 1);
    lua_setfield(state, param_index, type_name);
  }
  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
