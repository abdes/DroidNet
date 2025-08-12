//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Base/Types/Viewport.h>

#include <cstdint>
#include <numbers>
#include <string>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>

namespace {

//===----------------------------------------------------------------------===//

template <typename T>
concept HasToString = requires(const T& type) {
  { to_string(type) } -> std::convertible_to<std::string>;
};

#define CHECK_HAS_TO_STRING(T)                                                 \
  static_assert(HasToString<oxygen::T>, "T must have to_string implementation")

NOLINT_TEST(CommonTypes, HaveToString)
{
  CHECK_HAS_TO_STRING(PixelPosition);
  CHECK_HAS_TO_STRING(SubPixelPosition);
  CHECK_HAS_TO_STRING(PixelExtent);
  CHECK_HAS_TO_STRING(SubPixelExtent);
  CHECK_HAS_TO_STRING(PixelBounds);
  CHECK_HAS_TO_STRING(SubPixelBounds);
  CHECK_HAS_TO_STRING(PixelMotion);
  CHECK_HAS_TO_STRING(SubPixelMotion);
  CHECK_HAS_TO_STRING(Viewport);
  CHECK_HAS_TO_STRING(Axis1D);
  CHECK_HAS_TO_STRING(Axis2D);
}

NOLINT_TEST(CommonTypes, ConvertSecondsToDuration)
{
  constexpr float whole_value = 2.0F;
  constexpr int64_t whole_value_duration = 2'000'000;
  constexpr float fraction_value = .5F;
  constexpr int64_t fraction_value_duration = 500'000;

  EXPECT_EQ(
    oxygen::SecondsToDuration(whole_value).count(), whole_value_duration);
  EXPECT_EQ(
    oxygen::SecondsToDuration(fraction_value).count(), fraction_value_duration);
}

//===----------------------------------------------------------------------===//

NOLINT_TEST(EndianTest, IsLittleEndian_ChecksSystemEndianness)
{
  constexpr uint32_t value = 0x01234567;
  const auto bytes = std::as_bytes(std::span { &value, 1 });
  const bool is_little = bytes[0] == std::byte { 0x67 };

  EXPECT_EQ(oxygen::IsLittleEndian(), is_little);
}

//===----------------------------------------------------------------------===//

NOLINT_TEST(ByteSwapTest, ByteSwap_SingleByte_NoChange)
{
  constexpr uint8_t value = 0x12;
  EXPECT_EQ(oxygen::ByteSwap(value), value);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_16Bit)
{
  constexpr uint16_t value = 0x1234;
  constexpr uint16_t expected = 0x3412;
  EXPECT_EQ(oxygen::ByteSwap(value), expected);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_32Bit)
{
  constexpr uint32_t value = 0x12345678;
  constexpr uint32_t expected = 0x78563412;
  EXPECT_EQ(oxygen::ByteSwap(value), expected);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_64Bit)
{
  constexpr uint64_t value = 0x1234567890ABCDEF;
  constexpr uint64_t expected = 0xEFCDAB9078563412;
  EXPECT_EQ(oxygen::ByteSwap(value), expected);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_Float)
{
  union {
    float f;
    uint32_t i;
  } u1 = { std::numbers::pi_v<float> }, u2;

  u2.i = oxygen::ByteSwap(u1.i);
  u1.i = oxygen::ByteSwap(u2.i);
  EXPECT_FLOAT_EQ(u1.f, std::numbers::pi_v<float>);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_Double)
{
  union {
    double d;
    uint64_t i;
  } u1 = { std::numbers::pi }, u2;

  u2.i = oxygen::ByteSwap(u1.i);
  u1.i = oxygen::ByteSwap(u2.i);
  EXPECT_DOUBLE_EQ(u1.d, std::numbers::pi);
}

} // namespace
