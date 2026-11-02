//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>

#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::MipExtent;

TEST(TextureTypeTest, MipExtentHalvesPerLevel)
{
  EXPECT_EQ(MipExtent(1024U, 0U), 1024U);
  EXPECT_EQ(MipExtent(1024U, 1U), 512U);
  EXPECT_EQ(MipExtent(1023U, 1U), 511U);
  EXPECT_EQ(MipExtent(1024U, 10U), 1U);
}

TEST(TextureTypeTest, MipExtentNeverFallsBelowOneTexel)
{
  EXPECT_EQ(MipExtent(1024U, 11U), 1U);
  EXPECT_EQ(MipExtent(1U, 5U), 1U);
  EXPECT_EQ(MipExtent(0U, 0U), 1U);
}

//! A level past every bit of the extent is one texel, not a shift the
//! language leaves undefined.
TEST(TextureTypeTest, MipExtentOfALevelPastEveryBitIsOneTexel)
{
  constexpr auto kWidest = std::numeric_limits<uint32_t>::max();

  EXPECT_EQ(MipExtent(kWidest, 31U), 1U);
  EXPECT_EQ(MipExtent(kWidest, 32U), 1U);
  EXPECT_EQ(MipExtent(kWidest, 200U), 1U);
}

TEST(TextureTypeTest, MipChainLengthReachesOneTexel)
{
  EXPECT_EQ(oxygen::MipChainLength(1U, 1U), 1U);
  EXPECT_EQ(oxygen::MipChainLength(4U, 4U), 3U);
  EXPECT_EQ(oxygen::MipChainLength(100U, 100U), 7U);
  EXPECT_EQ(oxygen::MipChainLength(128U, 128U), 8U);
  EXPECT_EQ(oxygen::MipChainLength(1024U, 512U), 11U);
  EXPECT_EQ(oxygen::MipChainLength(1U, 4U), 3U);
  EXPECT_EQ(oxygen::MipChainLength(4U, 4U, 16U), 5U);
}

TEST(TextureTypeTest, AnEmptyExtentHasNoMipChain)
{
  EXPECT_EQ(oxygen::MipChainLength(0U, 0U), 0U);
  EXPECT_EQ(oxygen::MipChainLength(0U, 100U), 0U);
  EXPECT_EQ(oxygen::MipChainLength(100U, 0U), 0U);
  EXPECT_EQ(oxygen::MipChainLength(4U, 4U, 0U), 0U);
}

} // namespace
