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

//! Raw IEEE-754 binary16 bit pattern.
struct HalfFloat final : oxygen::NamedType<uint16_t, struct HalfFloatTag,
                           // clang-format off
      oxygen::Comparable,
      oxygen::Printable,
      oxygen::Hashable,
      oxygen::DefaultInitialized>
// clang-format on
{
  using Base = oxygen::NamedType<uint16_t, struct HalfFloatTag,
    // clang-format off
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable,
    oxygen::DefaultInitialized>;
  // clang-format on

  using Base::Base;

  //! Constructs a binary16 bit pattern by converting a 32-bit float.
  explicit constexpr HalfFloat(float value) noexcept
    : Base(Encode(value))
  {
  }

  //! Converts this binary16 bit pattern to a 32-bit float.
  [[nodiscard]] constexpr auto ToFloat() const noexcept -> float
  {
    return Decode(this->get());
  }

private:
  [[nodiscard]] static constexpr auto Encode(float value) noexcept -> uint16_t;
  [[nodiscard]] static constexpr auto Decode(uint16_t bits) noexcept -> float;
};

static_assert(sizeof(HalfFloat) == sizeof(uint16_t));
static_assert(std::is_trivially_copyable_v<HalfFloat>);

[[nodiscard]] constexpr auto HalfFloat::Encode(float value) noexcept -> uint16_t
{
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  const uint32_t sign = (bits >> 31) & 0x1U;
  const uint32_t exp = (bits >> 23) & 0xFFU;
  const uint32_t mantissa = bits & 0x7FFFFFU;

  const uint32_t sign_out = sign << 15;

  // NaN / Inf
  if (exp == 0xFFU) {
    if (mantissa == 0) {
      return static_cast<uint16_t>(sign_out | 0x7C00U);
    }
    // Quiet NaN, preserve some payload bits.
    const uint32_t payload = (mantissa >> 13) & 0x3FFU;
    return static_cast<uint16_t>(sign_out | 0x7C00U | payload | 0x0200U);
  }

  // Zero / subnormal in f32
  if (exp == 0) {
    return static_cast<uint16_t>(sign_out);
  }

  // Normalize exponent from f32 bias (127) to f16 bias (15).
  const int32_t exp_unbiased = static_cast<int32_t>(exp) - 127;
  const int32_t exp_half = exp_unbiased + 15;

  // Overflow -> Inf
  if (exp_half >= 31) {
    return static_cast<uint16_t>(sign_out | 0x7C00U);
  }

  // Underflow -> subnormal/zero
  if (exp_half <= 0) {
    // Too small even for subnormal.
    if (exp_half < -10) {
      return static_cast<uint16_t>(sign_out);
    }

    // Convert to subnormal half.
    uint32_t mant = mantissa | 0x800000U; // implicit leading 1
    const uint32_t shift = static_cast<uint32_t>(1 - exp_half);

    // We need to shift mantissa from 23-bit to 10-bit, plus extra shift for
    // subnormal exponent.
    const uint32_t total_shift = 13U + shift;

    uint32_t mant_out = mant >> total_shift;

    // Round to nearest-even.
    const uint32_t remainder_mask = (1U << total_shift) - 1U;
    const uint32_t remainder = mant & remainder_mask;
    const uint32_t halfway = 1U << (total_shift - 1U);

    if (remainder > halfway
      || (remainder == halfway && ((mant_out & 1U) != 0))) {
      mant_out += 1U;
    }

    // Rounding can carry from 0x03ff to the minimum normal encoding 0x0400.
    return static_cast<uint16_t>(sign_out | mant_out);
  }

  // Normalized half.
  uint32_t mant_out = mantissa >> 13;

  // Round to nearest-even.
  const uint32_t remainder = mantissa & 0x1FFFU;
  if (remainder > 0x1000U || (remainder == 0x1000U && ((mant_out & 1U) != 0))) {
    mant_out += 1U;
    if (mant_out == 0x400U) {
      // Mantissa overflow; increment exponent.
      mant_out = 0;
      const uint32_t exp_out = static_cast<uint32_t>(exp_half + 1);
      if (exp_out >= 31U) {
        return static_cast<uint16_t>(sign_out | 0x7C00U);
      }
      return static_cast<uint16_t>(sign_out | (exp_out << 10));
    }
  }

  return static_cast<uint16_t>(
    sign_out | (static_cast<uint32_t>(exp_half) << 10) | (mant_out & 0x3FFU));
}

[[nodiscard]] constexpr auto HalfFloat::Decode(uint16_t bits) noexcept -> float
{
  const uint32_t sign = (static_cast<uint32_t>(bits) >> 15) & 0x1U;
  const uint32_t exp = (static_cast<uint32_t>(bits) >> 10) & 0x1FU;
  const uint32_t mantissa = static_cast<uint32_t>(bits) & 0x3FFU;

  uint32_t out_sign = sign << 31;
  uint32_t out_exp = 0;
  uint32_t out_mant = 0;

  if (exp == 0) {
    if (mantissa == 0) {
      const uint32_t out_bits = out_sign;
      return std::bit_cast<float>(out_bits);
    }

    // Subnormal half -> normalized float.
    uint32_t mant = mantissa;
    uint32_t e = 1;
    while ((mant & 0x400U) == 0) {
      mant <<= 1U;
      e += 1U;
    }
    mant &= 0x3FFU;

    // Subnormals start at exponent 1-bias (-14), before normalization shifts.
    out_exp = static_cast<uint32_t>(127 - 14 - (e - 1));
    out_mant = mant << 13;

    const uint32_t out_bits = out_sign | (out_exp << 23) | out_mant;
    return std::bit_cast<float>(out_bits);
  }

  if (exp == 31) {
    // Inf/NaN
    out_exp = 0xFFU;
    out_mant = mantissa << 13;
    const uint32_t out_bits = out_sign | (out_exp << 23) | out_mant;
    return std::bit_cast<float>(out_bits);
  }

  out_exp = exp + (127 - 15);
  out_mant = mantissa << 13;

  const uint32_t out_bits = out_sign | (out_exp << 23) | out_mant;
  return std::bit_cast<float>(out_bits);
}

} // namespace oxygen::data
