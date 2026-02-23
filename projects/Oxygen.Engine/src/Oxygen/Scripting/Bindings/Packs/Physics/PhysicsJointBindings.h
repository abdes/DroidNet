//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

struct lua_State;

namespace oxygen::scripting::bindings {

auto RegisterPhysicsJointBindings(lua_State* state, int oxygen_table_index)
  -> void;

} // namespace oxygen::scripting::bindings
