//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {

//! Distinguishes two test patterns of the same size.
enum class PatternSeed : uint8_t {
  kFirst = 0,
  kSecond = 101,
};

//! Deterministic test bytes in which no two nearby offsets hold the same
//! value, so a read from a wrong offset never passes by accident.
[[nodiscard]] inline auto PatternBytes(const size_t size,
  const PatternSeed seed = PatternSeed::kFirst) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte>(size);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<std::byte>(
      ((i * 31U) + 7U + static_cast<uint8_t>(seed)) & 0xFFU);
  }
  return bytes;
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
