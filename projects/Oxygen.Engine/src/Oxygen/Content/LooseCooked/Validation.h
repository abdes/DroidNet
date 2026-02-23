//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content::lc {

OXGN_CNTT_API auto ValidateRoot(const std::filesystem::path& cooked_root)
  -> void;

} // namespace oxygen::content::lc
