//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting::detail {

//! Input of the Luau linter and type checker for one script.
struct LuauCheckRequest {
  std::string_view source_name;
  std::string_view source;
  //! Luau definitions of the `oxygen` API; empty to lint only, with `oxygen`
  //! declared `any`.
  std::string_view type_definitions;
  bool check_types = false;
};

//! Lints a parsed script and, on request, type-checks it in non-strict mode
//! (or strict when the script says `--!strict`), appending S04xx and S05xx
//! diagnostics.
/*!
 Each call builds its own Luau frontend, so calls are independent and safe
 from several threads.
*/
auto RunLuauChecks(const LuauCheckRequest& request,
  std::vector<ScriptDiagnostic>& diagnostics) -> void;

} // namespace oxygen::scripting::detail
