//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

/**
 * @brief Builds an ImportRequest from SceneImportSettings.
 * @param settings The settings to build from.
 * @param expected_format The expected format of the source file.
 * @param error_stream Stream to write errors to.
 * @return The build request, or nullopt if settings are invalid.
 */
OXGN_COOK_API auto BuildSceneRequest(const SceneImportSettings& settings,
  ImportFormat expected_format, std::ostream& error_stream)
  -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
