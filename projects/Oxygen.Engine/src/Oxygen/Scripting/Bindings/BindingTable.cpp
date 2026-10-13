//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <lua.h>

#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>

namespace oxygen::scripting::bindings {

auto RegisterFunctions(lua_State* state, const int table_index,
  const std::span<const BindingFunction> functions) -> void
{
  const int table = lua_absindex(state, table_index);
  for (const auto& entry : functions) {
    lua_pushcfunction(state, entry.function, entry.name);
    lua_setfield(state, table, entry.name);
  }
}

} // namespace oxygen::scripting::bindings
