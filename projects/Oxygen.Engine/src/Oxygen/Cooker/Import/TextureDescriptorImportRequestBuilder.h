//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>

#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::internal {

//! Build a normalized `ImportRequest` for schema-based texture descriptors.
OXGN_COOK_API auto BuildTextureDescriptorRequest(
  const TextureDescriptorImportSettings& settings, std::ostream& error_stream)
  -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
