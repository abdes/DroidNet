//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/TransformBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto LuaTransformSetLocalRotationEuler(lua_State* state) -> int
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

  auto LuaTransformSetLocalRotationQuat(lua_State* state) -> int
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
} // namespace

auto RegisterTransformBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "transform");

  lua_pushcfunction(state, LuaTransformSetLocalRotationEuler,
    "transform.set_local_rotation_euler");
  lua_setfield(state, module_index, "set_local_rotation_euler");

  lua_pushcfunction(state, LuaTransformSetLocalRotationQuat,
    "transform.set_local_rotation_quat");
  lua_setfield(state, module_index, "set_local_rotation_quat");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
