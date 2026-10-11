//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

struct lua_State;

namespace oxygen::scripting::bindings {

//! Registers `oxygen.script` and the `oxygen.param.<type>` constructors.
/*!
 Tools read a script's declaration statically (see ScriptAnalyzer); at
 runtime `oxygen.script` returns its table and each `oxygen.param.<type>`
 returns its table tagged with `type`, so a declaring script loads and runs.
*/
auto RegisterScriptDeclarationBindings(lua_State* state, int oxygen_table_index)
  -> void;

} // namespace oxygen::scripting::bindings
