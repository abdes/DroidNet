//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for script-asset import.
OXGN_COOK_API auto BuildScriptAssetRequest(
  const ScriptAssetImportSettings& settings, std::ostream& error_stream)
  -> std::optional<ImportRequest>;

//! Build a normalized `ImportRequest` for scripting sidecar import.
OXGN_COOK_API auto BuildScriptingSidecarRequest(
  const ScriptingSidecarImportSettings& settings, std::ostream& error_stream)
  -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
