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

auto RegisterContentUserdataMetatables(lua_State* state) -> void;
auto ContentUserdataClasses() -> std::span<const BindingClass>;

} // namespace oxygen::scripting::bindings
