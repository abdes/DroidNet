//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <cstdint>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/BufferResource.h>

using oxygen::data::BufferResource;

namespace {
//! Death tests covering BufferResource invariants (size vs stride alignment).
class BufferResourceDeathTest : public testing::Test { };

//! Ensures that constructing an index BufferResource whose size is not a
//! multiple of its element_stride (uint32 indices) triggers a fatal check.
NOLINT_TEST_F(BufferResourceDeathTest, IndexBufferSizeNotAligned_Throws)
{
#if !defined(NDEBUG)
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
#endif
}

//! Ensures that constructing a formatted BufferResource (element_format != 0)
//! with nonzero stride triggers a fatal check.
NOLINT_TEST_F(BufferResourceDeathTest, FormattedBufferNonzeroStride_Throws)
{
#if !defined(NDEBUG)
  // Arrange
  oxygen::data::pak::BufferResourceDesc bad_desc = { .data_offset = 0,
    .size_bytes = 16,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
    .element_stride = 4, // INVALID: must be 0 for formatted
    .element_format = 1, // formatted (not 0)
    .reserved = {} };
  std::vector<uint8_t> data(16, 0xAB);

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _
    = BufferResource(std::move(bad_desc), std::move(data)),
    "formatted buffer must have zero element_stride");
#endif
}

//! Ensures that constructing a structured BufferResource (element_format == 0,
//! stride > 1) with stride == 0 triggers a fatal check.
NOLINT_TEST_F(BufferResourceDeathTest, StructuredBufferZeroStride_Throws)
{
#if !defined(NDEBUG)
  // Arrange
  oxygen::data::pak::BufferResourceDesc bad_desc = { .data_offset = 0,
    .size_bytes = 16,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
    .element_stride = 0, // INVALID: stride cannot be zero for structured
    .element_format = 0, // structured
    .reserved = {} };
  std::vector<uint8_t> data(16, 0xAB);

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _
    = BufferResource(std::move(bad_desc), std::move(data)),
    "element_stride cannot be zero for structured buffer");
#endif
}

//! Basic tests covering BufferResource classification helpers (IsFormatted,
//! IsStructured, IsRaw) across representative descriptor variants.
class BufferResourceBasicTest : public testing::Test { };

//! Tests that formatted, structured, and raw buffer descriptors are classified
//! correctly by the helper methods.
NOLINT_TEST_F(BufferResourceBasicTest, ClassificationVariants_Correct)
{
  using oxygen::data::pak::BufferResourceDesc;

  // Arrange
  // Formatted buffer: element_format != 0 (e.g. R32G32B32A32Float), stride is
  // ignored per contract and should not influence classification.
  BufferResourceDesc formatted_desc {
    .data_offset = 0,
    .size_bytes = 16,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kStorageBuffer),
    .element_stride = 0, // ignored for formatted
    .element_format = static_cast<std::uint8_t>(oxygen::Format::kRGBA32Float),
    .reserved = {},
  };
  std::vector<uint8_t> formatted_bytes(16, 0xAB);

  // Structured buffer: element_format == 0 (kUnknown) and stride > 1.
  BufferResourceDesc structured_desc {
    .data_offset = 0,
    .size_bytes = 24,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kStorageBuffer),
    .element_stride = 12,
    .element_format = 0, // kUnknown
    .reserved = {},
  };
  std::vector<uint8_t> structured_bytes(24, 0xCD);

  // Raw buffer: element_format == 0 and stride == 1.
  BufferResourceDesc raw_desc {
    .data_offset = 0,
    .size_bytes = 8,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kStorageBuffer),
    .element_stride = 1,
    .element_format = 0, // kUnknown
    .reserved = {},
  };
  std::vector<uint8_t> raw_bytes(8, 0xEF);

  // Act
  BufferResource formatted { formatted_desc, std::move(formatted_bytes) };
  BufferResource structured { structured_desc, std::move(structured_bytes) };
  BufferResource raw { raw_desc, std::move(raw_bytes) };

  // Assert
  EXPECT_TRUE(formatted.IsFormatted());
  EXPECT_FALSE(formatted.IsStructured());
  EXPECT_FALSE(formatted.IsRaw());

  EXPECT_FALSE(structured.IsFormatted());
  EXPECT_TRUE(structured.IsStructured());
  EXPECT_FALSE(structured.IsRaw());

  EXPECT_FALSE(raw.IsFormatted());
  EXPECT_FALSE(raw.IsStructured());
  EXPECT_TRUE(raw.IsRaw());
}

//! Tests that data_offset from descriptor is preserved by accessor.
class BufferResourceDataOffsetTest : public testing::Test { };

//! Tests GetDataOffset returns the descriptor-provided value.
NOLINT_TEST_F(BufferResourceDataOffsetTest, DataOffsetPreserved)
{
  using oxygen::data::pak::BufferResourceDesc;
  constexpr std::uint64_t kOffset = 4096;
  BufferResourceDesc desc {
    .data_offset = kOffset,
    .size_bytes = 4,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kIndirectBuffer),
    .element_stride = 1,
    .element_format = 0,
    .reserved = {},
  };
  std::vector<uint8_t> bytes(4, 0x22);

  BufferResource resource { desc, std::move(bytes) };
  EXPECT_EQ(resource.GetDataOffset(), kOffset);
}

} // namespace

namespace {

//! Tests data size accessor correctness.
class BufferResourceDataSizeTest : public testing::Test { };

//! Tests that GetDataSize() matches the vector size passed at construction.
NOLINT_TEST_F(BufferResourceDataSizeTest, DataSizeMatchesDescriptor)
{
  using oxygen::data::pak::BufferResourceDesc;
  BufferResourceDesc desc {
    .data_offset = 0,
    .size_bytes = 48,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kStorageBuffer),
    .element_stride = 16,
    .element_format = 0,
    .reserved = {},
  };
  std::vector<uint8_t> bytes(48, 0x11);

  BufferResource resource { desc, std::move(bytes) };
  EXPECT_EQ(resource.GetDataSize(), 48u);
}

} // namespace

namespace {

//! Tests move semantics for BufferResource (ownership transfer of data).
class BufferResourceMoveTest : public testing::Test { };

//! Tests that move construction transfers data ownership and leaves source in
//! an empty state (size==0).
NOLINT_TEST_F(BufferResourceMoveTest, MoveConstructor_TransfersOwnership)
{
  using oxygen::data::pak::BufferResourceDesc;

  // Arrange
  BufferResourceDesc desc {
    .data_offset = 128,
    .size_bytes = 32,
    .usage_flags
    = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
    .element_stride = 1,
    .element_format = 0,
    .reserved = {},
  };
  std::vector<uint8_t> bytes(32, 0x5A);

  BufferResource original { desc, std::move(bytes) };
  ASSERT_EQ(original.GetDataSize(), 32u);

  // Act
  BufferResource moved { std::move(original) };

  // Assert
  EXPECT_EQ(moved.GetDataSize(), 32u);
  EXPECT_EQ(original.GetDataSize(), 0u); // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(moved.GetDataOffset(), 128u);
}

//! Tests to_string coverage for combined UsageFlags values.
class BufferResourceFlagsStringTest : public testing::Test { };

//! Tests that to_string includes all tokens for a representative combination
//! of flags.
NOLINT_TEST_F(BufferResourceFlagsStringTest, ToString_IncludesAllSetFlags)
{
  using F = BufferResource::UsageFlags;
  using oxygen::data::to_string; // ADL-safe

  // Arrange
  auto flags = F::kVertexBuffer | F::kIndexBuffer | F::kCPUReadable
    | F::kCPUWritable | F::kStatic;

  // Act
  auto text = to_string(flags);

  // Assert
  using ::testing::AllOf;
  using ::testing::HasSubstr;
  EXPECT_THAT(text,
    AllOf(HasSubstr("VertexBuffer"), HasSubstr("IndexBuffer"),
      HasSubstr("CPUReadable"), HasSubstr("CPUWritable"), HasSubstr("Static")));
}

} // namespace

namespace {

//! Tests covering bitwise operator helpers for BufferResource::UsageFlags.
class BufferResourceFlagsTest : public testing::Test { };

//! Tests that combining and masking flags preserves the expected bits.
NOLINT_TEST_F(BufferResourceFlagsTest, BitwiseCombination_PreservesBits)
{
  using F = BufferResource::UsageFlags;

  // Arrange
  auto combined = F::kVertexBuffer | F::kIndexBuffer | F::kCPUReadable
    | F::kCPUWritable | F::kDynamic;

  // Act
  auto with_removed = combined & ~(F::kCPUReadable | F::kDynamic);

  // Assert
  EXPECT_TRUE((combined & F::kVertexBuffer) == F::kVertexBuffer);
  EXPECT_TRUE((combined & F::kIndexBuffer) == F::kIndexBuffer);
  EXPECT_TRUE((combined & F::kCPUReadable) == F::kCPUReadable);
  EXPECT_TRUE((combined & F::kCPUWritable) == F::kCPUWritable);
  EXPECT_TRUE((combined & F::kDynamic) == F::kDynamic);

  EXPECT_TRUE((with_removed & F::kVertexBuffer) == F::kVertexBuffer);
  EXPECT_TRUE((with_removed & F::kIndexBuffer) == F::kIndexBuffer);
  EXPECT_TRUE((with_removed & F::kCPUWritable) == F::kCPUWritable);
  EXPECT_TRUE((with_removed & F::kCPUReadable) == F::kNone);
  EXPECT_TRUE((with_removed & F::kDynamic) == F::kNone);
}

} // namespace
