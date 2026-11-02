//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Format;
using oxygen::graphics::detail::BlockRows;
using oxygen::graphics::detail::RowBytes;

TEST(FormatUtilsTest, UncompressedRowsAreTexelsTimesTexelBytes)
{
  EXPECT_EQ(RowBytes(Format::kRGBA8UNorm, 64U), 256U);
  EXPECT_EQ(RowBytes(Format::kRGBA16Float, 3U), 24U);
  EXPECT_EQ(RowBytes(Format::kRGBA32Float, 1U), 16U);
  EXPECT_EQ(BlockRows(Format::kRGBA8UNorm, 7U), 7U);
}

TEST(FormatUtilsTest, BlockCompressedRowsAreWholeBlocks)
{
  EXPECT_EQ(RowBytes(Format::kBC7UNorm, 4U), 16U);
  EXPECT_EQ(RowBytes(Format::kBC7UNorm, 5U), 32U);
  EXPECT_EQ(RowBytes(Format::kBC7UNorm, 1U), 16U);
  EXPECT_EQ(BlockRows(Format::kBC7UNorm, 4U), 1U);
  EXPECT_EQ(BlockRows(Format::kBC7UNorm, 5U), 2U);
  EXPECT_EQ(BlockRows(Format::kBC7UNorm, 1U), 1U);
}

TEST(FormatUtilsTest, AFormatWithoutBlocksHasNoRows)
{
  EXPECT_EQ(RowBytes(Format::kUnknown, 16U), 0U);
  EXPECT_EQ(BlockRows(Format::kUnknown, 16U), 0U);
}

//! The widest row does not overflow: its bytes exceed 32 bits.
TEST(FormatUtilsTest, RowsOfTheWidestSurfaceDoNotOverflow)
{
  constexpr auto kWidest = std::numeric_limits<uint32_t>::max();

  EXPECT_EQ(
    RowBytes(Format::kRGBA32Float, kWidest), uint64_t { kWidest } * 16U);
  EXPECT_EQ(BlockRows(Format::kBC7UNorm, kWidest), (kWidest / 4U) + 1U);
}

} // namespace
