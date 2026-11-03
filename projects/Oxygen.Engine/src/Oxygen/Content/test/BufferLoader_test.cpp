//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "Fixtures/LoaderTestFixtures.h"

#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

using testing::NotNull;

using oxygen::Format;
using oxygen::content::loaders::LoadBufferResource;
using oxygen::cooker::testing::BufferRefusal;
using oxygen::cooker::testing::BufferRowRefusal;
using oxygen::data::BufferResource;
using oxygen::data::CookedDefect;
using oxygen::data::pak::core::BufferResourceDesc;
using Usage = oxygen::data::BufferResource::UsageFlags;

namespace {

//! Where the payload of every buffer row of these tests starts.
constexpr uint64_t kDataOffset = 256;

//! The usage flags `usage` as a buffer row stores them.
[[nodiscard]] constexpr auto Flags(const Usage usage) -> uint32_t
{
  return static_cast<uint32_t>(usage);
}

//! A buffer row for a payload of `size` bytes at `kDataOffset`.
[[nodiscard]] auto MakeDesc(const uint32_t size, const Usage usage,
  const uint32_t stride, const Format format) -> BufferResourceDesc
{
  return {
    .data_offset = kDataOffset,
    .size_bytes = size,
    .usage_flags = Flags(usage),
    .element_stride = stride,
    .element_format = static_cast<uint8_t>(format),
  };
}

//! The bytes of the row `desc`, as the descriptor stream stores them.
[[nodiscard]] auto RowBytes(const BufferResourceDesc& desc)
  -> std::span<const std::byte>
{
  return std::as_bytes(std::span { &desc, 1U });
}

//=== BufferLoader Basic Functionality Tests ===------------------------------//

//! Fixture that stages a buffer row and its payload as a mount reads them.
class BufferLoaderBasicTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  //! Loads the buffer `desc` names, whose payload holds `fill` bytes.
  auto Load(const BufferResourceDesc& desc, const std::byte fill)
    -> std::unique_ptr<BufferResource>
  {
    {
      auto pack = desc_writer_.ScopedAlignment(1);
      if (!desc_writer_.WriteBlob(RowBytes(desc))) {
        throw std::runtime_error("Failed to stage the buffer row");
      }
    }
    {
      auto data = std::vector<std::byte>(kDataOffset, std::byte { 0 });
      data.resize(kDataOffset + desc.size_bytes, fill);
      auto pack = data_writer_.ScopedAlignment(1);
      if (!data_writer_.WriteBlob(data)) {
        throw std::runtime_error("Failed to stage the buffer payload");
      }
    }
    const auto context
      = MakeResourceContext<oxygen::data::pak::core::BufferResourceDesc>();
    return LoadBufferResource(context);
  }

  //! Expects `asset` to hold `size` bytes, all `fill`.
  static auto ExpectPayload(const BufferResource& asset, const size_t size,
    const std::byte fill) -> void
  {
    EXPECT_EQ(asset.GetDataSize(), size);
    ASSERT_EQ(asset.GetData().size(), size);
    for (const auto& v : asset.GetData()) {
      EXPECT_EQ(v, static_cast<uint8_t>(fill));
    }
  }
};

//! A vertex buffer of a format has no stride, keeps its format and usage, and
//! reads as formatted.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBufferVertexBufferInputReturnsBufferAsset)
{
  // Arrange
  constexpr auto fill = std::byte { 0xAB };
  const auto desc
    = MakeDesc(192U, Usage::kVertexBuffer, 0U, Format::kRGB32Float);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 192U, fill);
  EXPECT_EQ(asset->GetElementStride(), 0U);
  EXPECT_EQ(asset->GetElementFormat(), Format::kRGB32Float);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kVertexBuffer);
  EXPECT_TRUE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! An index buffer of a format has no stride, keeps its format and usage, and
//! reads as formatted.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBufferIndexBufferInputReturnsBufferAsset)
{
  // Arrange
  constexpr auto fill = std::byte { 0x12 };
  const auto desc = MakeDesc(96U, Usage::kIndexBuffer, 0U, Format::kR32SInt);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 96U, fill);
  EXPECT_EQ(asset->GetElementStride(), 0U);
  EXPECT_EQ(asset->GetElementFormat(), Format::kR32SInt);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kIndexBuffer);
  EXPECT_TRUE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! A storage buffer of no format and a stride above one reads as structured.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBufferStructuredBufferReturnsBufferAsset)
{
  // Arrange
  constexpr auto fill = std::byte { 0xCD };
  const auto desc
    = MakeDesc(320U, Usage::kStorageBuffer, 64U, Format::kUnknown);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 320U, fill);
  EXPECT_EQ(asset->GetElementStride(), 64U);
  EXPECT_EQ(asset->GetElementFormat(), Format::kUnknown);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kStorageBuffer);
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_TRUE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! A constant buffer of no format and a stride of one reads as raw.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBufferRawBufferReturnsBufferAsset)
{
  // Arrange
  constexpr auto fill = std::byte { 0x5A };
  const auto desc
    = MakeDesc(128U, Usage::kConstantBuffer, 1U, Format::kUnknown);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 128U, fill);
  EXPECT_EQ(asset->GetElementStride(), 1U);
  EXPECT_EQ(asset->GetElementFormat(), Format::kUnknown);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kConstantBuffer);
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_TRUE(asset->IsRaw());
}

//! Several role flags of a row all reach the buffer.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBufferMultipleUsageFlagsReturnsBufferAsset)
{
  // Arrange
  constexpr auto fill = std::byte { 0x77 };
  const auto desc = MakeDesc(256U, Usage::kVertexBuffer | Usage::kStorageBuffer,
    0U, Format::kRGBA32Float);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 256U, fill);
  EXPECT_EQ(asset->GetElementFormat(), Format::kRGBA32Float);
  EXPECT_EQ(
    asset->GetUsageFlags(), Usage::kVertexBuffer | Usage::kStorageBuffer);
  EXPECT_TRUE(asset->IsFormatted());
}

//! The payload is the bytes at the row's `data_offset`, not those before it.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBufferAlignedDataOffsetWorks)
{
  // Arrange
  constexpr auto fill = std::byte { 0x99 };
  const auto desc
    = MakeDesc(32U, Usage::kIndirectBuffer, 0U, Format::kRG32UInt);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 32U, fill);
  EXPECT_EQ(asset->GetElementFormat(), Format::kRG32UInt);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kIndirectBuffer);
  EXPECT_TRUE(asset->IsFormatted());
}

//! A row of no payload loads an empty buffer.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBufferZeroDataSizeWorks)
{
  // Arrange
  const auto desc = MakeDesc(0U, Usage::kConstantBuffer, 0U, Format::kRG8UInt);

  // Act
  const auto asset = Load(desc, std::byte { 0x00 });

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 0U, std::byte { 0x00 });
  EXPECT_EQ(asset->GetElementFormat(), Format::kRG8UInt);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kConstantBuffer);
  EXPECT_TRUE(asset->IsFormatted());
}

//! The CPU access flags of a row reach the buffer, and a stride of no format
//! reads as structured.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBufferCPUAccessFlagsWorks)
{
  // Arrange
  constexpr auto fill = std::byte { 0xEE };
  const auto desc = MakeDesc(
    64U, Usage::kCPUWritable | Usage::kCPUReadable, 4U, Format::kUnknown);

  // Act
  const auto asset = Load(desc, fill);

  // Assert
  ASSERT_THAT(asset, NotNull());
  ExpectPayload(*asset, 64U, fill);
  EXPECT_EQ(asset->GetUsageFlags(), Usage::kCPUWritable | Usage::kCPUReadable);
  EXPECT_EQ(asset->GetElementStride(), 4U);
  EXPECT_TRUE(asset->IsStructured());
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsRaw());
}

//=== BufferLoader Error Handling Tests ===-----------------------------------//

//! A row cut short of a whole buffer row is refused as truncated.
NOLINT_TEST(BufferLoaderErrorTest, LoadBufferTruncatedHeaderThrows)
{
  // Arrange: the first 16 bytes of a row, up to its usage flags.
  const auto desc
    = MakeDesc(192U, Usage::kVertexBuffer, 0U, Format::kRGB32Float);
  const auto row = RowBytes(desc).first(16U);

  // Act + Assert
  EXPECT_EQ(BufferRowRefusal(row), CookedDefect::kTruncated);
}

//! A payload shorter than the row's `size_bytes` is refused as a size
//! mismatch.
NOLINT_TEST(BufferLoaderErrorTest, LoadBufferDataReadFailureThrows)
{
  // Arrange: a row of 128 bytes, a payload of 64.
  const auto desc = MakeDesc(128U, Usage::kVertexBuffer, 0U, Format::kRG8UInt);
  const auto payload = std::vector<std::byte>(64U, std::byte { 0x11 });

  // Act + Assert
  EXPECT_EQ(BufferRefusal(desc, payload), CookedDefect::kSizeMismatch);
}

} // namespace
