//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstdint>
#include <limits>

#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::Unorm16;

constexpr std::uint16_t kMax = 65535U;

NOLINT_TEST(Unorm16Test, NanEncodesAsZero)
{
  constexpr auto quiet = std::numeric_limits<float>::quiet_NaN();
  static_assert(Unorm16 { quiet }.get() == 0U);

  const auto signaling = std::numeric_limits<float>::signaling_NaN();
  const auto negative = std::bit_cast<float>(0xFFC00000U);
  const auto smallest_payload = std::bit_cast<float>(0x7F800001U);

  EXPECT_EQ(Unorm16 { quiet }.get(), 0U);
  EXPECT_EQ(Unorm16 { signaling }.get(), 0U);
  EXPECT_EQ(Unorm16 { negative }.get(), 0U);
  EXPECT_EQ(Unorm16 { smallest_payload }.get(), 0U);
}

NOLINT_TEST(Unorm16Test, InfinitiesAndOutOfRangeValuesClamp)
{
  constexpr auto infinity = std::numeric_limits<float>::infinity();

  EXPECT_EQ(Unorm16 { infinity }.get(), kMax);
  EXPECT_EQ(Unorm16 { -infinity }.get(), 0U);
  EXPECT_EQ(Unorm16 { 2.0F }.get(), kMax);
  EXPECT_EQ(Unorm16 { -1.0F }.get(), 0U);
  EXPECT_EQ(Unorm16 { -0.0F }.get(), 0U);
}

NOLINT_TEST(Unorm16Test, ValuesRoundToTheNearestStep)
{
  constexpr auto step = 1.0F / 65535.0F;

  EXPECT_EQ(Unorm16 { 0.0F }.get(), 0U);
  EXPECT_EQ(Unorm16 { 1.0F }.get(), kMax);
  EXPECT_EQ(Unorm16 { 0.5F }.get(), 32768U);
  EXPECT_EQ(Unorm16 { 1.49F * step }.get(), 1U);
  EXPECT_EQ(Unorm16 { 1.51F * step }.get(), 2U);
  EXPECT_EQ(Unorm16 { 1.0F - (0.51F * step) }.get(), kMax - 1U);
  EXPECT_EQ(Unorm16 { 1.0F - (0.49F * step) }.get(), kMax);
}

NOLINT_TEST(Unorm16Test, EveryValueSurvivesARoundTrip)
{
  for (std::uint32_t raw = 0U; raw <= kMax; ++raw) {
    const auto value = Unorm16 { static_cast<std::uint16_t>(raw) };
    ASSERT_EQ(Unorm16 { value.ToFloat() }.get(), raw) << raw;
  }
}

} // namespace
