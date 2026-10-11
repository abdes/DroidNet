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

  //! `oxygen.script(table)`: returns the script table.
  auto LuaScript(lua_State* state) -> int
  {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_settop(state, 1);
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
