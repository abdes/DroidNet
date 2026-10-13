//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Scripting/Bindings/BindingTable.h>

struct lua_State;

namespace oxygen::scripting::bindings {

auto RegisterContentBindings(lua_State* state, int oxygen_table_index) -> void;
auto DescribeContentBindings() -> std::span<const BindingNamespaceDescription>;

} // namespace oxygen::scripting::bindings
