//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content::pak {

class PakCatalogIo final {
public:
  OXGN_COOK_NDAPI static auto ToCanonicalJsonString(
    const data::PakCatalog& catalog) -> std::string;

  OXGN_COOK_NDAPI static auto Parse(std::string_view text)
    -> Result<data::PakCatalog>;

  OXGN_COOK_NDAPI static auto Read(const std::filesystem::path& path)
    -> Result<data::PakCatalog>;

  OXGN_COOK_NDAPI static auto Write(const std::filesystem::path& path,
    const data::PakCatalog& catalog) -> Result<void>;
};

} // namespace oxygen::content::pak
