//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/base/types.h"

#include <cstdint>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"

template <typename T>
concept has_to_string = requires(const T &type) {
  { to_string(type) } -> std::convertible_to<std::string>;
};

#define CHECK_HAS_TO_STRING(T)                                                 \
  static_assert(has_to_string<oxygen::T>,                                      \
                "T must have a to_string implementation")

// NOLINTNEXTLINE
TEST(CommonTypes, HaveToString) {
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

// NOLINTNEXTLINE
TEST(CommonTypes, ConvertSecondsToDuration) {
  constexpr float kWholeValue = 2.0F;
  constexpr uint64_t kWholeValueDuration = 2'000'000;
  constexpr float kFractionValue = .5F;
  constexpr uint64_t kFractionValueDuration = 500'000;

  EXPECT_EQ(oxygen::SecondsToDuration(kWholeValue).count(),
            kWholeValueDuration);
  EXPECT_EQ(oxygen::SecondsToDuration(kFractionValue).count(),
            kFractionValueDuration);
}
