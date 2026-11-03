//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Validates a physics material descriptor; no file or cooked-resource I/O.
/*!
 Parses `bytes` as a JSON object and checks it against the descriptor schema.

 @param bytes Descriptor document text.
 @param source_path Path the diagnostics name.
 @param diagnostics Receives an error for every refusal.
 @return The parsed document, or nothing when an error was added.
*/
OXGN_COOK_NDAPI auto PreparePhysicsMaterialDescriptor(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<nlohmann::json>;

} // namespace oxygen::content::import::internal
