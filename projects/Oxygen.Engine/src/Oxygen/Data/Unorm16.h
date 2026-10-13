//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::data {

struct Unorm16 final
  : oxygen::NamedType<uint16_t, struct Unorm16Tag, oxygen::Comparable,
      oxygen::Printable, oxygen::Hashable, oxygen::DefaultInitialized> {
  using Base
    = oxygen::NamedType<uint16_t, struct Unorm16Tag, oxygen::Comparable,
      oxygen::Printable, oxygen::Hashable, oxygen::DefaultInitialized>;
  using Base::Base;

  //! Constructs from a float in [0,1], clamping out-of-range values.
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
    const float clamped = value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
    const float scaled = clamped * 65535.0F + 0.5F;
    const uint32_t as_u32 = scaled >= 65535.0F
      ? 65535U
      : (scaled <= 0.0F ? 0U : static_cast<uint32_t>(scaled));
    return static_cast<uint16_t>(as_u32);
  }

  [[nodiscard]] static constexpr auto Decode(uint16_t u_value) noexcept -> float
  {
    return static_cast<float>(u_value) / 65535.0F;
  }
};
static_assert(sizeof(Unorm16) == sizeof(uint16_t));

} // namespace oxygen::data
