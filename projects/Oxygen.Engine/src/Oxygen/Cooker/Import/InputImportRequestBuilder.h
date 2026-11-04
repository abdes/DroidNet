//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for input import.
/*!
 The builder is orchestration-only: it validates source presence, carries the
 source's identity and marks input routing in `ImportRequest`. It does not
 inspect source JSON structure; the pipeline checks the identity.
*/
OXGN_COOK_API auto BuildInputImportRequest(const InputImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
