//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

auto FiniteReference(const std::uint16_t bits) -> float
{
  const auto exponent = (bits >> 10U) & 31U;
  const auto mantissa = bits & 1023U;
  const auto magnitude = exponent == 0U
    ? std::ldexp(static_cast<float>(mantissa), -24)
    : std::ldexp(
        static_cast<float>(1024U + mantissa), static_cast<int>(exponent) - 25);
  return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

NOLINT_TEST(HalfFloatTest, EveryBitPatternMatchesIndependentArithmetic)
{
  for (std::uint32_t raw = 0U; raw <= 65535U; ++raw) {
    const auto bits = static_cast<std::uint16_t>(raw);
    const auto decoded = oxygen::data::HalfFloat { bits }.ToFloat();
    if ((bits & 0x7c00U) == 0x7c00U) {
      if ((bits & 0x03ffU) != 0U) {
        ASSERT_TRUE(std::isnan(decoded)) << raw;
      } else {
        ASSERT_TRUE(std::isinf(decoded)) << raw;
        ASSERT_EQ(std::signbit(decoded), (bits & 0x8000U) != 0U) << raw;
      }
    } else {
      ASSERT_EQ(std::bit_cast<std::uint32_t>(decoded),
        std::bit_cast<std::uint32_t>(FiniteReference(bits)))
        << raw;
    }
  }
}

NOLINT_TEST(HalfFloatTest, EveryFiniteReferenceEncodesToItsOriginalBits)
{
  for (std::uint32_t raw = 0U; raw <= 65535U; ++raw) {
    const auto bits = static_cast<std::uint16_t>(raw);
    if ((bits & 0x7c00U) == 0x7c00U)
      continue;
    ASSERT_EQ(oxygen::data::HalfFloat { FiniteReference(bits) }.get(), bits)
      << raw;
  }
}

NOLINT_TEST(HalfFloatTest, SubnormalRoundingCarriesIntoMinimumNormal)
{
  struct Case {
    std::uint32_t input;
    std::uint16_t expected;
  };
  // The midpoint 2^-14 - 2^-25 ties to the even minimum-normal significand.
  const std::array cases { Case { 0x387fdfffU, 0x03ffU },
    Case { 0x387fe000U, 0x0400U }, Case { 0x387fe001U, 0x0400U },
    Case { 0x387fffffU, 0x0400U }, Case { 0xb87fdfffU, 0x83ffU },
    Case { 0xb87fe000U, 0x8400U }, Case { 0xb87fe001U, 0x8400U },
    Case { 0xb87fffffU, 0x8400U } };
  for (const auto test : cases) {
    EXPECT_EQ(
      oxygen::data::HalfFloat { std::bit_cast<float>(test.input) }.get(),
      test.expected)
      << test.input;
  }
}

} // namespace
