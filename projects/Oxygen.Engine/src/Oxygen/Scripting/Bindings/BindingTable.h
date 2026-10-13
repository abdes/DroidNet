//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/api_export.h>

struct lua_State;

namespace oxygen::scripting::bindings {

//! Sets each function as a field of the table at `table_index`.
OXGN_SCRP_API auto RegisterFunctions(lua_State* state, int table_index,
  std::span<const BindingFunction> functions) -> void;

} // namespace oxygen::scripting::bindings
