//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/Hex.h>

namespace oxygen::base {

namespace {

  constexpr auto kDigits = std::string_view { "0123456789abcdef" };
  constexpr auto kNibbleBits = 4U;
  constexpr auto kNibbleMask = 0x0FU;
  constexpr auto kLetterBase = 10U;

  constexpr auto ParseNibble(const char c) noexcept -> std::optional<unsigned>
  {
    if (c >= '0' && c <= '9') {
      return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return kLetterBase + static_cast<unsigned>(c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
      return kLetterBase + static_cast<unsigned>(c - 'A');
    }
    return std::nullopt;
  }

} // namespace

auto ToHexString(const std::span<const std::byte> bytes) -> std::string
{
  auto text = std::string {};
  text.reserve(bytes.size() * 2U);
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    text.push_back(kDigits[value >> kNibbleBits]);
    text.push_back(kDigits[value & kNibbleMask]);
  }
  return text;
}

auto FromHexString(
  const std::string_view text, const std::span<std::byte> out) noexcept -> bool
{
  if (text.size() != out.size() * 2U) {
    return false;
  }
  for (auto index = size_t { 0U }; index < out.size(); ++index) {
    const auto high = ParseNibble(text[index * 2U]);
    const auto low = ParseNibble(text[(index * 2U) + 1U]);
    if (!high.has_value() || !low.has_value()) {
      return false;
    }
    out[index] = static_cast<std::byte>((*high << kNibbleBits) | *low);
  }
  return true;
}

} // namespace oxygen::base
