//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>


namespace oxygen::content::import {

//! Severity of an import diagnostic.
enum class ImportSeverity : uint8_t {
  kInfo = 0,
  kWarning,
  kError,
};

//! Convert an import severity to a string label.
[[nodiscard]] inline auto to_string(ImportSeverity severity) -> std::string_view
{
  switch (severity) {
  case ImportSeverity::kInfo:
    return "Info";
  case ImportSeverity::kWarning:
    return "Warning";
  case ImportSeverity::kError:
    return "Error";
  }
  return "Unknown";
}



//! One diagnostic emitted during import.
struct ImportDiagnostic final {
  ImportSeverity severity = ImportSeverity::kInfo;

  //! Stable identifier (e.g. "gltf.missing_buffer").
  std::string code;

  //! Human-readable message.
  std::string message;

  //! Optional path to the source file.
  std::string source_path;

  //! Optional hierarchical object path (node/material/mesh).
  std::string object_path;
};

} // namespace oxygen::content::import
