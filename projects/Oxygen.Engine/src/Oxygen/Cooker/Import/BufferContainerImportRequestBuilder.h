//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string_view>

#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for schema-based buffer containers.
/*!
 `identity` is the container's virtual path, the anchor of the products it
 names, and `mounts` locates its sources: both as the job uses them.
*/
OXGN_COOK_API auto BuildBufferContainerRequest(
  const BufferContainerImportSettings& settings, std::string_view identity,
  const MountTable& mounts, std::ostream& error_stream,
  std::shared_ptr<const CapturedInputSet> captured_inputs = {})
  -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
