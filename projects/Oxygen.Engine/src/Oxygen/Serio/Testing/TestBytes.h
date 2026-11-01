//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {

//! Distinguishes two test patterns of the same size.
enum class PatternSeed : uint8_t {
  kFirst = 0,
  kSecond = 101,
};

namespace detail {
  //! A key for the 256-byte block `block` (the SplitMix64 finalizer), so
  //! blocks differ and the pattern has no period.
  [[nodiscard]] constexpr auto BlockKey(const size_t block) noexcept -> uint8_t
  {
    auto mixed = static_cast<uint64_t>(block) + 0x9E37'79B9'7F4A'7C15ULL;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return static_cast<uint8_t>((mixed ^ (mixed >> 31U)) & 0xFFU);
  }
} // namespace detail

//! Deterministic test bytes in which no two offsets of one 256-byte block
//! hold the same value and blocks differ, so a read from a wrong offset, a
//! multiple of 256 away included, never passes by accident.
[[nodiscard]] inline auto PatternBytes(const size_t size,
  const PatternSeed seed = PatternSeed::kFirst) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte>(size);
  for (size_t i = 0; i < size; ++i) {
    const auto in_block = ((i * 31U) + 7U + static_cast<uint8_t>(seed)) & 0xFFU;
    bytes[i] = static_cast<std::byte>(in_block ^ detail::BlockKey(i / 256U));
  }
  return bytes;
}

//! A buffer holding a copy of `bytes`, for a resource or a device to own.
/*!
 @throw std::bad_alloc When the buffer cannot be allocated.
*/
[[nodiscard]] inline auto OwnedBytes(const std::span<const std::byte> bytes)
  -> ByteBuffer
{
  auto buffer = ByteBuffer::CopyOf(bytes);
  if (!buffer) {
    throw std::bad_alloc();
  }
  return std::move(buffer).value();
}

//! `OwnedBytes` of unsigned bytes.
[[nodiscard]] inline auto OwnedBytes(const std::span<const uint8_t> bytes)
  -> ByteBuffer
{
  return OwnedBytes(std::as_bytes(bytes));
}

//! Matches a contiguous range of bytes equal to the expected bytes.
/*!
 Compares in one pass and reports the sizes and the first differing offset,
 so it stays fast and readable on buffers of megabytes, where
 `ElementsAreArray` describes every element.

 ```cpp
 EXPECT_THAT(stream.Data(), BytesEqual(expected));
 ```
*/
class BytesEqualMatcher {
public:
  using is_gtest_matcher = void;

  explicit BytesEqualMatcher(std::vector<std::byte> expected)
    : expected_(std::move(expected))
  {
  }

  template <class Range>
  auto MatchAndExplain(const Range& actual, std::ostream* listener) const
    -> bool
  {
    const auto bytes = std::span<const std::byte>(actual);
    if (bytes.size() != expected_.size()) {
      if (listener != nullptr) {
        *listener << "holds " << bytes.size() << " bytes";
      }
      return false;
    }
    const auto [differs, _] = std::ranges::mismatch(bytes, expected_);
    if (differs == bytes.end()) {
      return true;
    }
    if (listener != nullptr) {
      *listener << "first differs at offset " << (differs - bytes.begin());
    }
    return false;
  }

  auto DescribeTo(std::ostream* os) const -> void
  {
    *os << "holds the " << expected_.size() << " expected bytes";
  }

  auto DescribeNegationTo(std::ostream* os) const -> void
  {
    *os << "does not hold the " << expected_.size() << " expected bytes";
  }

private:
  std::vector<std::byte> expected_;
};

//! Matches bytes equal to `expected`; see `BytesEqualMatcher`.
[[nodiscard]] inline auto BytesEqual(const std::span<const std::byte> expected)
  -> BytesEqualMatcher
{
  return BytesEqualMatcher({ expected.begin(), expected.end() });
}

} // namespace oxygen::serio::testing
