//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::lc {

OXGN_COOK_API auto ValidateRoot(const std::filesystem::path& cooked_root,
  IntegrityCheck check = IntegrityCheck::kFull) -> void;

} // namespace oxygen::content::lc
