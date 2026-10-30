//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::base::ComputeCrc32;
using oxygen::base::Crc32;

auto ToBytes(const std::string_view text) -> std::span<const std::byte>
{
  return std::as_bytes(std::span { text });
}

//! Longer than the eight-byte slice so splits land on both table loops.
constexpr auto kPatternSize = size_t { 37U };
//! Several four-lane folding blocks plus every 16-byte and byte remainder.
constexpr auto kLongPatternSize = size_t { 1100U };
//! Below the 64-byte folding minimum, so piecewise updates use the tables.
constexpr auto kTablePieceSize = size_t { 7U };
constexpr auto kMaxAlignment = size_t { 16U };

template <size_t N> auto MakePattern() -> std::array<std::byte, N>
{
  constexpr auto kMultiplier = 131U;
  constexpr auto kIncrement = 7U;
  auto bytes = std::array<std::byte, N> {};
  auto state = 0U;
  for (auto& byte : bytes) {
    state = (state * kMultiplier) + kIncrement;
    byte = static_cast<std::byte>((state >> 8U) & 0xFFU);
  }
  return bytes;
}

//! The IEEE CRC-32 gives the standard check values.
NOLINT_TEST(Crc32Test, MatchesStandardCheckValues)
{
  EXPECT_EQ(ComputeCrc32(ToBytes("123456789")), 0xCBF43926U);
  EXPECT_EQ(ComputeCrc32(ToBytes("a")), 0xE8B7BE43U);
  EXPECT_EQ(
    ComputeCrc32(ToBytes("The quick brown fox jumps over the lazy dog")),
    0x414FA339U);
}

//! No bytes checksum to zero, whether hashed at once or never updated.
NOLINT_TEST(Crc32Test, EmptyInputIsZero)
{
  EXPECT_EQ(ComputeCrc32({}), 0U);
  EXPECT_EQ(Crc32 {}.Finalize(), 0U);

  auto crc = Crc32 {};
  crc.Update({});
  EXPECT_EQ(crc.Finalize(), 0U);
}

//! Any division of the input into three pieces gives the one-call value.
NOLINT_TEST(Crc32Test, PiecewiseUpdatesMatchOneCall)
{
  const auto pattern = MakePattern<kPatternSize>();
  const auto bytes = std::span<const std::byte> { pattern };
  const auto expected = ComputeCrc32(bytes);

  for (auto first = size_t { 0U }; first <= bytes.size(); ++first) {
    for (auto second = first; second <= bytes.size(); ++second) {
      auto crc = Crc32 {};
      crc.Update(bytes.first(first));
      crc.Update(bytes.subspan(first, second - first));
      crc.Update(bytes.subspan(second));
      EXPECT_EQ(crc.Finalize(), expected)
        << "split at " << first << " and " << second;
    }
  }
}

//! Any split of a long buffer, where pieces of 64 bytes or more take the
//! folding path when the CPU has it, gives the one-call value.
NOLINT_TEST(Crc32Test, SplitsAcrossTheFoldingMinimumMatchOneCall)
{
  const auto pattern = MakePattern<kLongPatternSize>();
  const auto bytes = std::span<const std::byte> { pattern };
  const auto expected = ComputeCrc32(bytes);

  for (auto split = size_t { 0U }; split <= bytes.size(); ++split) {
    auto crc = Crc32 {};
    crc.Update(bytes.first(split));
    crc.Update(bytes.subspan(split));
    EXPECT_EQ(crc.Finalize(), expected) << "split at " << split;
  }
}

//! Table-only updates agree with one call at every length and alignment, so
//! the folding path is checked against the portable one.
NOLINT_TEST(Crc32Test, OneCallMatchesTablePathAtEveryLengthAndAlignment)
{
  const auto pattern = MakePattern<kLongPatternSize + kMaxAlignment>();
  const auto all = std::span<const std::byte> { pattern };

  for (auto offset = size_t { 0U }; offset < kMaxAlignment; ++offset) {
    const auto bytes = all.subspan(offset, kLongPatternSize);
    auto table = Crc32 {};
    for (auto length = size_t { 0U }; length <= bytes.size(); ++length) {
      if (length > 0U) {
        table.Update(bytes.subspan(length - 1U, 1U));
      }
      ASSERT_EQ(ComputeCrc32(bytes.first(length)), table.Finalize())
        << "offset " << offset << ", length " << length;
    }
    auto pieces = Crc32 {};
    for (auto start = size_t { 0U }; start < bytes.size();
      start += kTablePieceSize) {
      pieces.Update(bytes.subspan(start).first(
        (std::min)(kTablePieceSize, bytes.size() - start)));
    }
    EXPECT_EQ(pieces.Finalize(), ComputeCrc32(bytes)) << "offset " << offset;
  }
}

//! Finalize reports the bytes so far and lets more follow.
NOLINT_TEST(Crc32Test, FinalizeKeepsTheState)
{
  const auto text = ToBytes("123456789");
  auto crc = Crc32 {};
  crc.Update(text.first(4U));

  EXPECT_EQ(crc.Finalize(), ComputeCrc32(text.first(4U)));
  crc.Update(text.subspan(4U));
  EXPECT_EQ(crc.Finalize(), 0xCBF43926U);
}

} // namespace
