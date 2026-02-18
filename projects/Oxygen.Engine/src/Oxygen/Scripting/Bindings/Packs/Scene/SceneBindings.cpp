//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto LuaSceneGetParam(lua_State* state) -> int
  {
    return GetParamValue(state);
  }
} // namespace

auto RegisterSceneBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "scene");

  lua_pushcfunction(state, LuaSceneGetParam, "scene.get_param");
  lua_setfield(state, module_index, "get_param");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
