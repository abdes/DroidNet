//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

struct lua_State;

namespace oxygen::scripting::detail {

//! One script function on the stack when a Lua error was raised.
struct ScriptFrame {
  std::string source; //!< Chunk name without its `@`.
  uint32_t line = 0;
  std::string function; //!< Empty when Luau knows no name.
};

//! Pushes a `lua_pcall` message handler that records the script frames.
/*!
 The handler runs before the stack unwinds and replaces the contents of
 `frames` with the frames whose chunk name starts with `@`, innermost first.
 It returns the error object unchanged. `frames` must outlive the call.
*/
auto PushErrorFrameCollector(lua_State* state, std::vector<ScriptFrame>* frames)
  -> void;

//! Input of a runtime (S0701) diagnostic.
struct RuntimeErrorInput {
  //! The Lua error object as text, usually `source:line: message`.
  std::string_view error_message;
  //! Script frames captured while the error was raised, innermost first.
  std::span<const ScriptFrame> frames;
  //! Name used for the outermost frame when Luau knows none: the hook.
  std::string_view entry_name;
  const SourceProvider* sources = nullptr;
  //! True when `name` is a `node` or `asset` parameter of the slot without a
  //! value.
  std::function<bool(std::string_view name)> is_unset_reference;
};

//! Builds the S0701 diagnostic of a runtime error.
/*!
 The location in the message, or else the innermost script frame, is the
 primary label; outer frames become `called from` notes. When the error indexes
 `nil` through `self.params.<name>` and that parameter is an unset `node` or
 `asset` reference, the label narrows to the expression and the diagnostic
 explains how to set the parameter.
*/
auto MakeRuntimeDiagnostic(const RuntimeErrorInput& input) -> ScriptDiagnostic;

} // namespace oxygen::scripting::detail
