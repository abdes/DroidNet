//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! Input of one Luau compilation.
struct LuauCompileInput {
  //! Virtual or project-relative path used in diagnostic spans.
  std::string_view source_name;
  std::span<const std::byte> source;
  core::meta::scripting::ScriptCompileMode mode
    = core::meta::scripting::ScriptCompileMode::kDebug;
};

//! Result of one Luau compilation.
struct LuauCompileOutput {
  //! Luau bytecode; empty when compilation failed.
  std::vector<std::byte> bytecode;
  //! Syntax (S01xx) and compile (S02xx) diagnostics.
  std::vector<ScriptDiagnostic> diagnostics;

  [[nodiscard]] auto Succeeded() const noexcept -> bool
  {
    return !bytecode.empty();
  }
};

//! Compiles Luau source to bytecode, reporting failures as diagnostics.
/*!
 Every syntax error becomes an S0101 diagnostic and a compiler error an S0201
 diagnostic, each with its source span; empty source is S0204. Debug mode
 keeps full debug information and no optimization, so runtime errors map to
 source lines; optimized mode drops debug information.

 Stateless and safe to use from several threads. The engine's script compiler
 and the cooker share it, so both report the same diagnostics.
*/
OXGN_SCAN_NDAPI auto CompileLuau(const LuauCompileInput& input)
  -> LuauCompileOutput;

} // namespace oxygen::scripting
