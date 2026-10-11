//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! Producer family of a diagnostic code; the hundreds digit of the code.
enum class DiagnosticFamily : uint8_t {
  kSyntax = 1,
  kCompile = 2,
  kDeclarations = 3,
  kLint = 4,
  kTypes = 5,
  kBinding = 6,
  kRuntime = 7,
};
OXGN_SCAN_NDAPI auto to_string(DiagnosticFamily value) noexcept
  -> std::string_view;

//! Catalog entry of one diagnostic code.
/*!
 `title` names the problem in one line. `explanation` is the short text the
 editor shows on demand, like `rustc --explain`: what the code means and how
 to resolve it.
*/
struct DiagnosticInfo {
  DiagnosticCode code = DiagnosticCode::kSyntaxError;
  std::string_view title;
  std::string_view explanation;
};

//! Returns the family a code belongs to.
OXGN_SCAN_NDAPI auto FamilyOf(DiagnosticCode code) noexcept -> DiagnosticFamily;

//! Returns every cataloged code, in ascending code order.
OXGN_SCAN_NDAPI auto DiagnosticCatalog() noexcept
  -> std::span<const DiagnosticInfo>;

//! Returns the catalog entry of `code`, or null when it is not cataloged.
OXGN_SCAN_NDAPI auto Describe(DiagnosticCode code) noexcept
  -> const DiagnosticInfo*;

//! Parses a code as written in diagnostics, for example `S0302`.
/*!
 @return The code, or nothing when the text is malformed or names a code the
 catalog does not contain.
*/
OXGN_SCAN_NDAPI auto ParseDiagnosticCode(std::string_view text) noexcept
  -> std::optional<DiagnosticCode>;

} // namespace oxygen::scripting
