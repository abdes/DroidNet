//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace oxygen::content::import::internal {

[[nodiscard]] constexpr auto DedupCollisionAction(
  const DedupCollisionPolicy policy) noexcept -> std::string_view
{
  if (policy == DedupCollisionPolicy::kWarnReplace) {
    return "replace";
  }
  if (policy == DedupCollisionPolicy::kWarnKeepFirst) {
    return "keep_first";
  }
  return "error";
}

} // namespace oxygen::content::import::internal
