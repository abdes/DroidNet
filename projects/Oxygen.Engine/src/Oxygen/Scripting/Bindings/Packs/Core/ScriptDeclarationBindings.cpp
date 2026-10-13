//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <span>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/ScriptDeclarationBindings.h>

namespace oxygen::scripting::bindings {

namespace {

  //! Registry key of the weak-keyed set of tables `oxygen.script` returned.
  constexpr const char* kScriptTablesKey = "oxygen.script.tables";

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

  constexpr auto kScriptFunctions = std::to_array<BindingFunction>({
    {
      .name = "script",
      .function = LuaScript,
      .signature = "<P>(declaration: { params: P? }) -> OxygenScript<P>",
    },
  });

  //! One constructor per parameter type, named after it; the name is also
  //! the type tag the constructor stores. Each returns, for type checking,
  //! the type of the value the parameter has in `self.params`.
  constexpr auto kParamFunctions = std::to_array<BindingFunction>({
    {
      .name = "bool",
      .function = LuaParam,
      .signature = "(options: ParamBoolOptions) -> boolean",
    },
    {
      .name = "int",
      .function = LuaParam,
      .signature = "(options: ParamNumberOptions) -> number",
    },
    {
      .name = "float",
      .function = LuaParam,
      .signature = "(options: ParamNumberOptions) -> number",
    },
    {
      .name = "string",
      .function = LuaParam,
      .signature = "(options: ParamStringOptions) -> string",
    },
    {
      .name = "vec2",
      .function = LuaParam,
      .signature = "(options: ParamVectorOptions) -> vector",
    },
    {
      .name = "vec3",
      .function = LuaParam,
      .signature = "(options: ParamVectorOptions) -> vector",
    },
    {
      .name = "vec4",
      .function = LuaParam,
      .signature = "(options: ParamVectorOptions) -> { number }",
    },
    {
      .name = "asset",
      .function = LuaParam,
      .signature = "(options: ParamAssetOptions) -> string?",
    },
    {
      .name = "node",
      .function = LuaParam,
      .signature = "(options: ParamNodeOptions) -> SceneNode?",
    },
  });

  constexpr auto kScriptTypes = std::to_array<BindingTypeAlias>({
    {
      .name = "OxygenScript<P>",
      .definition = R"({
  node: SceneNode,
  params: P,
  on_start: ((self: OxygenScript<P>) -> ())?,
  on_fixed_simulation: ((self: OxygenScript<P>, dt: number) -> ())?,
  on_gameplay: ((self: OxygenScript<P>, dt: number) -> ())?,
  on_scene_mutation: ((self: OxygenScript<P>, dt: number) -> ())?,
  on_destroy: ((self: OxygenScript<P>) -> ())?,
  [string]: any,
})",
    },
    {
      .name = "ParamBoolOptions",
      .definition = "{ default: boolean?, description: string? }",
    },
    {
      .name = "ParamNumberOptions",
      .definition = "{ default: number?, min: number?, max: number?, "
                    "step: number?, unit: string?, description: string? }",
    },
    {
      .name = "ParamStringOptions",
      .definition = "{ default: string?, options: { string }?, "
                    "description: string? }",
    },
    {
      .name = "ParamVectorOptions",
      .definition = "{ default: { number }?, min: number?, max: number?, "
                    "step: number?, unit: string?, color: boolean?, "
                    "description: string? }",
    },
    {
      .name = "ParamAssetOptions",
      .definition = "{ kind: string, description: string? }",
    },
    {
      .name = "ParamNodeOptions",
      .definition = "{ description: string? }",
    },
  });

  constexpr auto kScriptDeclarationDescription
    = std::to_array<BindingNamespaceDescription>({
      { .path = "", .functions = kScriptFunctions, .types = kScriptTypes },
      { .path = "param", .functions = kParamFunctions },
    });

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
  RegisterFunctions(state, oxygen_index, kScriptFunctions);

  const int param_index = PushOxygenSubtable(state, oxygen_index, "param");
  for (const auto& entry : kParamFunctions) {
    lua_pushstring(state, entry.name);
    lua_pushcclosure(state, entry.function, entry.name, 1);
    lua_setfield(state, param_index, entry.name);
  }
  lua_pop(state, 1);
}

auto DescribeScriptDeclarationBindings()
  -> std::span<const BindingNamespaceDescription>
{
  return kScriptDeclarationDescription;
}

} // namespace oxygen::scripting::bindings
