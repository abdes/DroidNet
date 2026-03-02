//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for input import.
/*!
 The builder is orchestration-only: it validates source presence, marks input
 routing in `ImportRequest`, and optionally carries batch dependency metadata.
 It does not inspect source JSON structure or infer asset kind.
*/
OXGN_COOK_API auto BuildInputImportRequest(const InputImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>;

//! Build a normalized `ImportRequest` for input import with manifest metadata.
OXGN_COOK_API auto BuildInputImportRequest(const InputImportSettings& settings,
  std::string job_id, std::vector<std::string> depends_on,
  std::ostream& error_stream) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
