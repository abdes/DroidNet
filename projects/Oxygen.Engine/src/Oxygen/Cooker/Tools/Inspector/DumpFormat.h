//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace oxygen::content::inspection {

inline constexpr std::string_view kHexDigits = "0123456789abcdef";
inline constexpr unsigned kNibbleBits = 4U;
inline constexpr unsigned kNibbleMask = 0x0FU;

//! Width of the size column, shared by the tabular listings.
inline constexpr int kSizeColumn = 10;

[[nodiscard]] inline auto HexHigh(const uint8_t byte) -> char
{
  return kHexDigits.at(
    (static_cast<unsigned>(byte) >> kNibbleBits) & kNibbleMask);
}

[[nodiscard]] inline auto HexLow(const uint8_t byte) -> char
{
  return kHexDigits.at(static_cast<unsigned>(byte) & kNibbleMask);
}

[[nodiscard]] inline auto ToHex64(const uint64_t value) -> std::string
{
  std::ostringstream ss;
  ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return ss.str();
}

} // namespace oxygen::content::inspection
