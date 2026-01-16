//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>

#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportSettings.h>

namespace oxygen::content::import::tool {

[[nodiscard]] auto BuildTextureRequest(const TextureImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::tool
