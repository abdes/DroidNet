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

//! Registers `oxygen.script` and the `oxygen.param.<type>` constructors.
/*!
 Tools read a script's declaration statically (see ScriptAnalyzer); at
 runtime `oxygen.script` records and returns its table and each
 `oxygen.param.<type>` returns its table tagged with `type`, so a declaring
 script loads and runs.
*/
auto RegisterScriptDeclarationBindings(lua_State* state, int oxygen_table_index)
  -> void;
auto DescribeScriptDeclarationBindings()
  -> std::span<const BindingNamespaceDescription>;

//! Whether the value at `index` is a table returned by `oxygen.script`.
auto IsScriptTable(lua_State* state, int index) -> bool;

} // namespace oxygen::scripting::bindings
