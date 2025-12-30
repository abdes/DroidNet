//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Serio/Writer.h>

#include "Mocks/MockStream.h"
#include "Utils/PakUtils.h"

using testing::NotNull;

using oxygen::content::loaders::LoadBufferResource;
using oxygen::serio::Reader;

namespace {

//=== BufferLoader Basic Functionality Tests ===------------------------------//

//! Fixture for BufferLoader basic serialization tests.
class BufferLoaderBasicTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  BufferLoaderBasicTest()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  //! Helper method to create LoaderContext for testing.
  auto CreateLoaderContext() -> oxygen::content::LoaderContext
  {
    if (!desc_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek data_stream");
    }

    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .desc_reader = &desc_reader_,
      .data_readers = std::make_tuple(&data_reader_, &data_reader_),
      .work_offline = false,
    };
  }

  MockStream desc_stream_;
  MockStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader<MockStream> desc_reader_;
  Reader<MockStream> data_reader_;
};

//! Test: LoadBufferResource returns valid BufferResource for vertex buffer
//! input.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBuffer_VertexBufferInput_ReturnsBufferAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a valid BufferResourceDesc header (32 bytes), padded
  // to 256 (to place the buffer data after)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256    (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 192    (C0 00 00 00)
  //   0x0C: usage_flags      = 1      (01 00 00 00) // kVertexBuffer
  //   0x10: element_stride   = 0      (0C 00 00 00)
  //   0x14: element_format   = 27     (1B) // kRGB32Float
  //   0x15: reserved[11]     = {0}    (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
    16: 00 00 00 00 1B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 192;
  constexpr auto fill_value = std::byte { 0xAB };

  // Write header and 192 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetDataSize(), 192U);
  EXPECT_EQ(asset->GetElementStride(), 0);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kRGB32Float);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer));
  EXPECT_EQ(asset->GetData().size(), 192U);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<const uint8_t>(fill_value));
  }
  EXPECT_TRUE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! Test: LoadBufferResource returns valid BufferResource for index buffer
//! input.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBuffer_IndexBufferInput_ReturnsBufferAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a valid BufferResourceDesc header (32 bytes), with
  // index buffer usage and R16UInt format.
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 96    (60 00 00 00)
  //   0x0C: usage_flags      = 2     (02 00 00 00) // kIndexBuffer
  //   0x10: element_stride   = 0     (00 00 00 00) // Ignored for formatted
  //   0x14: element_format   = 11    (0B) // kR32SInt
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 60 00 00 00 02 00 00 00
    16: 00 00 00 00 0B 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 96;
  constexpr auto fill_value = std::byte { 0x12 };

  // Write header and 96 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetDataSize(), 96U);
  EXPECT_EQ(asset->GetElementStride(), 0U);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kR32SInt);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer));
  EXPECT_EQ(asset->GetData().size(), 96U);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<uint8_t>(fill_value));
  }
  EXPECT_TRUE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! Test: LoadBufferResource returns valid BufferResource for structured buffer.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBuffer_StructuredBuffer_ReturnsBufferAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a structured buffer (element_format = 0, stride > 1).
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 320   (40 01 00 00)
  //   0x0C: usage_flags      = 8     (08 00 00 00) // kStorageBuffer
  //   0x10: element_stride   = 64    (40 00 00 00)
  //   0x14: element_format   = 0     (00) // kUnknown (structured)
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 40 01 00 00 08 00 00 00
    16: 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 320;
  constexpr auto fill_value = std::byte { 0xCD };

  // Write header and 320 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetDataSize(), 320U);
  EXPECT_EQ(asset->GetElementStride(), 64U);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kUnknown);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kStorageBuffer));
  EXPECT_EQ(asset->GetData().size(), 320U);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<uint8_t>(fill_value));
  }
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_TRUE(asset->IsStructured());
  EXPECT_FALSE(asset->IsRaw());
}

//! Test: LoadBufferResource returns valid BufferResource for raw buffer.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBuffer_RawBuffer_ReturnsBufferAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a raw buffer (element_format = 0, stride = 1).
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 128   (80 00 00 00)
  //   0x0C: usage_flags      = 4     (04 00 00 00) // kConstantBuffer
  //   0x10: element_stride   = 1     (01 00 00 00)
  //   0x14: element_format   = 0     (00) // kUnknown (raw)
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 04 00 00 00
    16: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 128;
  constexpr auto fill_value = std::byte { 0x5A };

  // Write header and 128 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetDataSize(), 128U);
  EXPECT_EQ(asset->GetElementStride(), 1U);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kUnknown);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kConstantBuffer));
  EXPECT_EQ(asset->GetData().size(), 128U);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<uint8_t>(fill_value));
  }
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsStructured());
  EXPECT_TRUE(asset->IsRaw());
}

//! Test: LoadBufferResource returns kUnknown for invalid element format.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBuffer_InvalidElementFormat_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a BufferResourceDesc header (32 bytes), with
  // element_format = 255 (invalid). Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 64    (40 00 00 00)
  //   0x0C: usage_flags      = 1     (01 00 00 00) // kVertexBuffer
  //   0x10: element_stride   = 0     (00 00 00 00)
  //   0x14: element_format   = 255   (FF) <- invalid
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 40 00 00 00 01 00 00 00
    16: 00 00 00 00 FF 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 64;
  constexpr auto fill_value = std::byte { 0x33 };

  // Write header and 64 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetElementFormat(), Format::kUnknown);
}

//! Test: LoadBufferResource correctly handles multiple usage flags.
NOLINT_TEST_F(
  BufferLoaderBasicTest, LoadBuffer_MultipleUsageFlags_ReturnsBufferAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a buffer with multiple usage flags (vertex + storage).
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 256   (00 01 00 00)
  //   0x0C: usage_flags      = 9     (09 00 00 00) // kVertexBuffer | kStorageBuffer
  //   0x10: element_stride   = 0     (00 00 00 00)
  //   0x14: element_format   = 42    (2A) // kRGBA32Float
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 00 01 00 00 09 00 00 00
    16: 00 00 00 00 2A 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 256;
  constexpr auto fill_value = std::byte { 0x77 };

  // Write header and 256 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kRGBA32Float);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer
      | BufferResource::UsageFlags::kStorageBuffer));
  EXPECT_EQ(asset->GetData().size(), 256U);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<uint8_t>(fill_value));
  }
  EXPECT_TRUE(asset->IsFormatted());
}

//! Test: LoadBufferResource correctly handles a non-zero data_offset.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBuffer_AlignedDataOffset_Works)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a BufferResourceDesc header (32 bytes), with
  // data_offset = 256 and size_bytes = 32. Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 32    (20 00 00 00)
  //   0x0C: usage_flags      = 16    (10 00 00 00) // kIndirectBuffer
  //   0x10: element_stride   = 0     (00 00 00 00)
  //   0x14: element_format   = 22    (16) // kRG32UInt
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 20 00 00 00 10 00 00 00
    16: 00 00 00 00 16 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 32;
  constexpr auto fill_value = std::byte { 0x99 };

  // Write header and 288 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetData().size(), size_bytes);
  for (const auto& v : asset->GetData()) {
    EXPECT_EQ(v, static_cast<const uint8_t>(fill_value));
  }
  EXPECT_EQ(asset->GetDataSize(), 32U);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kRG32UInt);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kIndirectBuffer));
  EXPECT_TRUE(asset->IsFormatted());
}

//! Test: LoadBufferResource handles zero size_bytes (no buffer data)
//! gracefully.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBuffer_ZeroDataSize_Works)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a BufferResourceDesc header (32 bytes), size_bytes =
  // 0.
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 0     (00 00 00 00)
  //   0x0C: usage_flags      = 4     (04 00 00 00) // kConstantBuffer
  //   0x10: element_stride   = 0     (00 00 00 00)
  //   0x14: element_format   = 13    (0D) // kRG8UInt
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 00 00 00 00 04 00 00 00
    16: 00 00 00 00 0D 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 0;
  constexpr auto fill_value = std::byte { 0x00 };

  // Write header only (no buffer data needed)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetData().size(), 0U);
  EXPECT_EQ(asset->GetDataSize(), 0U);
  EXPECT_EQ(asset->GetElementFormat(), oxygen::Format::kRG8UInt);
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kConstantBuffer));
  EXPECT_TRUE(asset->IsFormatted());
}

//! Test: LoadBufferResource handles CPU access flags correctly.
NOLINT_TEST_F(BufferLoaderBasicTest, LoadBuffer_CPUAccessFlags_Works)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::BufferResource;

  // Arrange: Hexdump for a buffer with CPU read/write access.
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256   (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 64    (40 00 00 00)
  //   0x0C: usage_flags      = 96    (60 00 00 00) // kCPUWritable | kCPUReadable
  //   0x10: element_stride   = 4     (04 00 00 00)
  //   0x14: element_format   = 0     (00) // kUnknown (structured)
  //   0x15: reserved[11]     = {0}   (00 00 00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 40 00 00 00 60 00 00 00
    16: 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 64;
  constexpr auto fill_value = std::byte { 0xEE };

  // Write header and 64 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadBufferResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(static_cast<uint32_t>(asset->GetUsageFlags()),
    static_cast<uint32_t>(BufferResource::UsageFlags::kCPUWritable
      | BufferResource::UsageFlags::kCPUReadable));
  EXPECT_EQ(asset->GetElementStride(), 4U);
  EXPECT_TRUE(asset->IsStructured());
  EXPECT_FALSE(asset->IsFormatted());
  EXPECT_FALSE(asset->IsRaw());
}

//=== BufferLoader Error Handling Tests ===-----------------------------------//

//! Fixture for BufferLoader error test cases.
class BufferLoaderErrorTest : public BufferLoaderBasicTest {
  // No additional members needed for now; extend as needed for error scenarios.
};

//! Test: LoadBufferResource throws if the header is truncated (less than 32
//! bytes).
NOLINT_TEST_F(BufferLoaderErrorTest, LoadBuffer_TruncatedHeader_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Write only 16 bytes (less than the required 32 bytes for header)
  const std::string truncated_hexdump = R"(
     0: 00 01 00 00 00 00 00 00 C0 00 00 00 01 00 00 00
  )";

  // Write incomplete header, no buffer data
  WriteDescriptorWithData(
    desc_writer_, data_writer_, truncated_hexdump, 0, std::byte { 0x00 });

  // Act + Assert: Should throw due to incomplete header
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadBufferResource(context); }, std::runtime_error);
}

//! Test: LoadBufferResource throws if data reading fails.
NOLINT_TEST_F(BufferLoaderErrorTest, LoadBuffer_DataReadFailure_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Valid header but insufficient data in stream
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 01 00 00 00
    16: 00 00 00 00 0D 00 00 00 00 00 00 00 00 00 00 00
  )";

  // Write header but less data than specified in size_bytes
  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump, 64,
    std::byte { 0x11 }); // Only 64 bytes instead of 256+128

  // Act + Assert: Should throw due to insufficient data
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadBufferResource(context); }, std::runtime_error);
}

} // namespace
