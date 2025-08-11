//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <cstddef>
#include <cstdint>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/GeometryAsset.h>

using oxygen::data::detail::IndexBufferView;
using oxygen::data::detail::IndexType;

namespace {
//! Fixture for basic IndexBufferView slicing behavior tests.
class IndexBufferViewSliceTest : public testing::Test { };

//! (20) SliceElements: Valid slice produces view with correct element count &
//! byte size alignment.
NOLINT_TEST_F(IndexBufferViewSliceTest, SliceElements_ValidProducesCorrectCount)
{
  // Arrange
  // Build a 32-bit index buffer of 8 elements (32 bytes total).
  std::vector<std::uint32_t> indices { 0, 2, 4, 6, 8, 10, 12, 14 };
  auto bytes = std::as_bytes(
    std::span<const std::uint32_t>(indices.data(), indices.size()));
  IndexBufferView full { bytes, IndexType::kUInt32 };
  ASSERT_FALSE(full.Empty());
  ASSERT_EQ(full.Count(), indices.size());

  // Act
  auto slice = full.SliceElements(2, 3); // elements [2,3,4] => values 4,6,8

  // Assert
  EXPECT_FALSE(slice.Empty());
  EXPECT_EQ(slice.type, IndexType::kUInt32);
  EXPECT_EQ(slice.Count(), 3u);
  EXPECT_EQ(slice.bytes.size(), 3u * sizeof(std::uint32_t));
  auto as_u32 = slice.AsU32();
  ASSERT_EQ(as_u32.size(), 3u);
  EXPECT_EQ(as_u32[0], 4u);
  EXPECT_EQ(as_u32[1], 6u);
  EXPECT_EQ(as_u32[2], 8u);
}

//! (21) SliceElements: Invalid (out-of-range) slice returns empty view.
NOLINT_TEST_F(IndexBufferViewSliceTest, SliceElements_InvalidReturnsEmpty)
{
  // Arrange
  std::vector<std::uint32_t> indices { 0, 1, 2, 3 };
  auto bytes = std::as_bytes(
    std::span<const std::uint32_t>(indices.data(), indices.size()));
  IndexBufferView full { bytes, IndexType::kUInt32 };
  ASSERT_EQ(full.Count(), 4u);

  // Act
  // Start inside but length too large (overflow end) -> empty
  auto overflow_slice = full.SliceElements(2, 5);
  // Start exactly past end -> empty
  auto past_end_slice = full.SliceElements(4, 1);

  // Assert
  EXPECT_TRUE(overflow_slice.Empty());
  EXPECT_TRUE(past_end_slice.Empty());
  EXPECT_EQ(overflow_slice.Count(), 0u);
  EXPECT_EQ(past_end_slice.Count(), 0u);
}

//! Fixture for widened iteration behavior on sliced views.
class IndexBufferViewWidenedTest : public testing::Test { };

//! (22) Widened iteration over a sliced 16-bit view matches manual extraction.
NOLINT_TEST_F(
  IndexBufferViewWidenedTest, WidenedIteration_OnSliceMatchesExpected)
{
  // Arrange
  std::vector<std::uint16_t> indices16 { 10, 11, 12, 13, 14, 15 };
  auto bytes = std::as_bytes(
    std::span<const std::uint16_t>(indices16.data(), indices16.size()));
  IndexBufferView full { bytes, IndexType::kUInt16 };
  ASSERT_EQ(full.Count(), indices16.size());

  auto slice = full.SliceElements(1, 4); // values 11,12,13,14
  ASSERT_EQ(slice.Count(), 4u);

  // Act
  std::vector<uint32_t> widened;
  for (auto v : slice.Widened()) {
    widened.push_back(v);
  }

  // Assert
  ASSERT_EQ(widened.size(), 4u);
  EXPECT_EQ(widened[0], 11u);
  EXPECT_EQ(widened[1], 12u);
  EXPECT_EQ(widened[2], 13u);
  EXPECT_EQ(widened[3], 14u);
}

//! Fixture for invariant-related IndexBufferView tests.
class IndexBufferViewInvariantsTest : public testing::Test { };

//! (23) Empty() returns true when type is kNone regardless of byte span size.
NOLINT_TEST_F(IndexBufferViewInvariantsTest, EmptyWhenTypeNone)
{
  // Arrange
  std::vector<std::uint32_t> indices { 1, 2, 3 };
  auto bytes = std::as_bytes(
    std::span<const std::uint32_t>(indices.data(), indices.size()));

  // Act
  IndexBufferView with_type_none { bytes, IndexType::kNone };

  // Assert
  EXPECT_TRUE(with_type_none.Empty())
    << "IndexBufferView should report empty when type==kNone even if bytes are "
       "non-empty.";
  EXPECT_EQ(with_type_none.Count(), 0u);
  EXPECT_EQ(with_type_none.ElementSize(), 0u);
}

} // namespace
