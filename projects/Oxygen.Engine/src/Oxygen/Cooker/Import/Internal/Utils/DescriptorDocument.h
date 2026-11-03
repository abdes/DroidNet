//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <span>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>

namespace oxygen::content::import::internal {

//! Writes each diagnostic as one `SEVERITY [code]: message (at 'pointer')`
//! line, the form request builders and manifest loading report on a stream.
inline auto WriteDiagnostics(
  const std::span<const ImportDiagnostic> diagnostics, std::ostream& errors)
  -> void
{
  for (const auto& diagnostic : diagnostics) {
    switch (diagnostic.severity) {
    case ImportSeverity::kError:
      errors << "ERROR";
      break;
    case ImportSeverity::kWarning:
      errors << "WARNING";
      break;
    case ImportSeverity::kInfo:
      errors << "INFO";
      break;
    }
    errors << " [" << diagnostic.code << "]: " << diagnostic.message;
    if (!diagnostic.json_pointer.empty()) {
      errors << " (at '" << diagnostic.json_pointer << "')";
    }
    errors << "\n";
  }
}

} // namespace oxygen::content::import::internal
