//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/PhysicsResourceDescriptorImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for physics resource descriptors.
OXGN_COOK_API auto BuildPhysicsResourceDescriptorRequest(
  const PhysicsResourceDescriptorImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
