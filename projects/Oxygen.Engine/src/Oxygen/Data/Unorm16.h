//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::data {

struct Unorm16 final
  : oxygen::NamedType<uint16_t, struct Unorm16Tag, oxygen::Comparable,
      oxygen::Printable, oxygen::Hashable, oxygen::DefaultInitialized> {
  using Base
    = oxygen::NamedType<uint16_t, struct Unorm16Tag, oxygen::Comparable,
      oxygen::Printable, oxygen::Hashable, oxygen::DefaultInitialized>;
  using Base::Base;

  //! Constructs from a float in [0,1], clamping out-of-range values; NaN
  //! encodes as 0.
  explicit constexpr Unorm16(float value) noexcept
    : Base(Encode(value))
  {
  }

  //! Converts to a float in [0,1].
  [[nodiscard]] constexpr auto ToFloat() const noexcept -> float
  {
    return Decode(get());
  }

private:
  [[nodiscard]] static constexpr auto Encode(float value) noexcept -> uint16_t
  {
    // NaN fails every comparison below, so its bits identify it: all exponent
    // bits set and a non-zero mantissa.
    const auto bits = std::bit_cast<uint32_t>(value);
    const bool is_nan
      = (bits & 0x7F800000U) == 0x7F800000U && (bits & 0x007FFFFFU) != 0U;
    if (is_nan || value <= 0.0F) {
      return 0U;
    }
    if (value >= 1.0F) {
      return 65535U;
    }
    // Rounds half up by comparing the fraction, which is exact since scaled is
    // below 65535; adding 0.5 before truncating can round up a value just
    // below a half. std::lround would make the constructor non-constexpr.
    const float scaled = value * 65535.0F;
    const auto whole = static_cast<uint16_t>(scaled);
    const bool rounds_up = scaled - static_cast<float>(whole) >= 0.5F;
    return rounds_up ? static_cast<uint16_t>(whole + 1U) : whole;
  }

  [[nodiscard]] static constexpr auto Decode(uint16_t u_value) noexcept -> float
  {
    return static_cast<float>(u_value) / 65535.0F;
  }
};
static_assert(sizeof(Unorm16) == sizeof(uint16_t));

} // namespace oxygen::data
