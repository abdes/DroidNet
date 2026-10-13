//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Testing/GTest.h>

namespace {

//===----------------------------------------------------------------------===//

template <typename T>
concept HasToString = requires(const T& type) {
  { to_string(type) } -> std::convertible_to<std::string>;
};

template <typename T>
concept ByteSwappable = requires(const T& value) { oxygen::ByteSwap(value); };

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
  CHECK_HAS_TO_STRING(Axis1D);
  CHECK_HAS_TO_STRING(Axis2D);
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

NOLINT_TEST(ByteSwapTest, ByteSwap_Float_SwapsRepresentationNotValue)
{
  // std::numbers::pi_v<float> is 0x40490FDB in IEEE-754 binary32; the
  // bit-reversed representation is 0xDB0F4940 (the pre-bit_cast version
  // truncated pi to the integer 3 instead).
  constexpr float value = std::numbers::pi_v<float>;
  const auto swapped = oxygen::ByteSwap(value);

  EXPECT_EQ(std::bit_cast<uint32_t>(swapped), uint32_t { 0xDB0F4940 });
  EXPECT_FLOAT_EQ(oxygen::ByteSwap(swapped), value);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_Double_SwapsRepresentationNotValue)
{
  // std::numbers::pi is 0x400921FB54442D18 in IEEE-754 binary64.
  constexpr double value = std::numbers::pi;
  const auto swapped = oxygen::ByteSwap(value);

  EXPECT_EQ(std::bit_cast<uint64_t>(swapped), uint64_t { 0x182D4454FB210940 });
  EXPECT_DOUBLE_EQ(oxygen::ByteSwap(swapped), value);
}

NOLINT_TEST(ByteSwapTest, ByteSwap_UnsupportedTypesAreIllFormed)
{
  // Only trivially copyable 1/2/4/8-byte values are byte-swappable; other
  // sizes must fail to compile rather than reach an unreachable path.
  struct alignas(1) ThreeBytes {
    uint8_t bytes[3];
  };
  struct FourByteNonTriviallyCopyable {
    FourByteNonTriviallyCopyable() = default;
    FourByteNonTriviallyCopyable(const FourByteNonTriviallyCopyable&) { }
    uint32_t x { 0 };
  };

  EXPECT_TRUE(ByteSwappable<uint8_t>);
  EXPECT_TRUE(ByteSwappable<uint32_t>);
  EXPECT_TRUE(ByteSwappable<float>);
  EXPECT_TRUE(ByteSwappable<double>);
  EXPECT_FALSE(ByteSwappable<ThreeBytes>);
  EXPECT_FALSE(ByteSwappable<FourByteNonTriviallyCopyable>);
}

} // namespace
