//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace oxygen::content::inspection {

//! Width of the size column, shared by the tabular listings.
inline constexpr int kSizeColumn = 10;

//! The value as `0x` and sixteen lowercase hexadecimal digits.
[[nodiscard]] inline auto ToHex64(const uint64_t value) -> std::string
{
  return fmt::format("{:#018x}", value);
}

} // namespace oxygen::content::inspection
