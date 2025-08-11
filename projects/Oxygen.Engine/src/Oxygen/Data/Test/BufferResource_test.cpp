//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::data::BufferResource;

namespace {
//! Death tests covering BufferResource invariants (size vs stride alignment).
class BufferResourceDeathTest : public testing::Test { };

//! Ensures that constructing an index BufferResource whose size is not a
//! multiple of its element_stride (uint32 indices) triggers a fatal check.
NOLINT_TEST_F(BufferResourceDeathTest, IndexBufferSizeNotAligned_Throws)
{
  // Arrange
  // Crafted descriptor: size_bytes=3, element_stride=4 (invalid alignment)
  oxygen::data::pak::BufferResourceDesc bad_desc = { .data_offset = 0,
    .size_bytes = 3, // not divisible by 4
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer),
    .element_stride = sizeof(std::uint32_t),
    .element_format = 0, // raw/structured (unknown format)
    .reserved = {} };
  std::vector<uint8_t> data(3, 0xCD);

  // Act
  // (Construction attempt is part of the assertion expression below.)

  // Assert
  EXPECT_DEATH([[maybe_unused]] auto _
    = BufferResource(std::move(bad_desc), std::move(data)),
    "not aligned to element stride");
}

} // namespace
