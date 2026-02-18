//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <lua.h>

#include <Oxygen/Scripting/Bindings/BindingRegistry.h>

namespace oxygen::scripting::bindings {

auto RegisterBindingNamespaces(lua_State* state, const int runtime_env_ref,
  const std::span<const BindingNamespace> namespaces) -> bool
{
  if (state == nullptr) {
    return false;
  }
  if (runtime_env_ref == LUA_NOREF || runtime_env_ref == LUA_REFNIL) {
    return false;
  }

  lua_getref(state, runtime_env_ref);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return false;
  }
  const int env_index = lua_gettop(state);

  lua_getfield(state, env_index, "oxygen");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, env_index, "oxygen");
  }
  const int oxygen_index = lua_gettop(state);

  lua_getfield(state, oxygen_index, "__namespaces");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, oxygen_index, "__namespaces");
  }
  const int namespaces_index = lua_gettop(state);

  for (const auto& descriptor : namespaces) {
    if (descriptor.register_fn != nullptr) {
      descriptor.register_fn(state, oxygen_index);
    }

    if (descriptor.name != nullptr && descriptor.name[0] != '\0') {
      lua_pushboolean(state, 1);
      lua_setfield(state, namespaces_index, descriptor.name);
    }
  }

  lua_pop(state, 3); // namespaces + oxygen + env
  return true;
}

} // namespace oxygen::scripting::bindings
