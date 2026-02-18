//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/TimeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto LuaTimeDeltaSeconds(lua_State* state) -> int
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

auto RegisterTimeBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "time");

  lua_pushcfunction(state, LuaTimeDeltaSeconds, "time.delta_seconds");
  lua_setfield(state, module_index, "delta_seconds");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
