//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Content/LooseCooked/Index.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::lc {

class Reader final {
public:
  OXGN_CNTT_API static auto LoadFromRoot(
    const std::filesystem::path& cooked_root) -> Index;
  OXGN_CNTT_API static auto LoadFromFile(
    const std::filesystem::path& index_path) -> Index;
};

} // namespace oxygen::content::lc
