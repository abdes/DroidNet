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

auto RegisterContentModuleProcedural(lua_State* state, int module_index)
  -> void;
auto ContentProceduralFunctions() -> std::span<const BindingFunction>;

} // namespace oxygen::scripting::bindings
