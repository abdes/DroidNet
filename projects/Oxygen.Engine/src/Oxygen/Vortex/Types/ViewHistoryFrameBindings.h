//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Types/Generated.ViewContracts.h>

namespace oxygen::vortex {

enum class ViewHistoryValidityFlagBits : std::uint32_t {
  kPreviousViewValid = 1U << 0U,
};

[[nodiscard]] constexpr auto HasAnyViewHistoryValidityFlag(
  const std::uint32_t flags, const ViewHistoryValidityFlagBits bits) noexcept
  -> bool
{
  return (flags & static_cast<std::uint32_t>(bits)) != 0U;
}

} // namespace oxygen::vortex
