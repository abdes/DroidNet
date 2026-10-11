//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! Version of the JSON form written by `ToJson`.
inline constexpr uint32_t kScriptDiagnosticJsonVersion = 1;

//! Writes a diagnostic as one JSON object.
/*!
 The object follows `oxygen.script-diagnostic.schema.json`: the version, the
 severity, the code (`S0302`) and its family, the message, labels with their
 spans, notes, and the optional help and slot context. Tools that receive
 diagnostics as a stream, such as the player's diagnostics channel, use this
 form.
*/
OXGN_SCAN_NDAPI auto ToJson(const ScriptDiagnostic& diagnostic) -> std::string;

//! Reads a diagnostic written by `ToJson`.
/*!
 @return The diagnostic, or a message naming what is malformed. A code that
 the catalog does not contain is malformed.
*/
OXGN_SCAN_NDAPI auto ParseDiagnosticJson(std::string_view json)
  -> std::expected<ScriptDiagnostic, std::string>;

} // namespace oxygen::scripting
