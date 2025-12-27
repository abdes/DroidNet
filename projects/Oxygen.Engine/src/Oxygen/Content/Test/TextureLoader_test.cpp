//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Writer.h>

#include "Mocks/MockStream.h"
#include "Utils/PakUtils.h"

using testing::NotNull;

using oxygen::content::loaders::LoadTextureResource;
using oxygen::serio::Reader;

namespace {

//=== TextureLoader Basic Functionality Tests ===-----------------------------//

//! Fixture for TextureLoader basic serialization tests.
class TextureLoaderBasicTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  TextureLoaderBasicTest()
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
      .asset_loader = nullptr, // Resources don't use asset_loader
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

//! Test: LoadTextureResource returns valid TextureResource for correct input.
NOLINT_TEST_F(
  TextureLoaderBasicTest, LoadTexture_ValidInput_ReturnsTextureAsset)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a valid TextureResourceDesc header (40 bytes), padded
  // to 256 (to place the texture data after)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 287         (1F 01 00 00)
  //   0x0C: texture_type     = 4           (04)  // kTexture2DArray
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 128         (80 00 00 00)
  //   0x12: height           = 64          (40 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 6           (06 00)
  //   0x1A: mip_levels       = 5           (05 00)
  //   0x1C: format           = 2           (02)  // kR8SInt
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 1F 01 00 00 04 00 80 00
    16: 00 00 40 00 00 00 01 00 06 00 05 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 287;
  constexpr auto fill_value = std::byte { 0x99 };

  // Write header and 287 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetWidth(), 128u);
  EXPECT_EQ(asset->GetHeight(), 64u);
  EXPECT_EQ(asset->GetDepth(), 1u);
  EXPECT_EQ(asset->GetArrayLayers(), 6u);
  EXPECT_EQ(asset->GetMipCount(), 5u);
  EXPECT_EQ(asset->GetFormat(), oxygen::Format::kR8SInt);
  EXPECT_EQ(asset->GetData().size(), 287u);
  EXPECT_THAT(
    asset->GetData(), ::testing::Each(static_cast<uint8_t>(fill_value)));
  EXPECT_EQ(asset->GetDataAlignment(), 256u);
  EXPECT_EQ(asset->GetTextureType(), oxygen::TextureType::kTexture2DArray);
  EXPECT_EQ(asset->GetCompressionType(), 0u);
}

//! Test: LoadTextureResource returns kUnknown for invalid format.
NOLINT_TEST_F(TextureLoaderBasicTest, LoadTexture_InvalidFormat_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // format = 255 (invalid), padded to 256 (to place the texture data after)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 128         (80 00 00 00)
  //   0x0C: texture_type     = 1           (01)  // kTexture1D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 1           (01 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 255         (FF) <- invalid
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 01 00 10 00
    16: 00 00 01 00 00 00 01 00 01 00 01 00 FF 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 128;
  constexpr auto fill_value = std::byte { 0x22 };

  // Write header and 128 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetFormat(), Format::kUnknown);
}

//! Test: LoadTextureResource correctly handles a non-zero data_offset.
NOLINT_TEST_F(TextureLoaderBasicTest, LoadTexture_AlignedDataOffset_Works)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // data_offset = 256 and size_bytes = 16.
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 16          (10 00 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 8           (08 00 00 00)
  //   0x12: height           = 8           (08 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 6           (06)  // kR16SInt
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 03 00 08 00
    16: 00 00 08 00 00 00 01 00 01 00 01 00 06 00 01 00
    32: 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;
  constexpr auto fill_value = std::byte { 0x5A };

  // Write header and 272 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetData().size(), size_bytes);
  EXPECT_THAT(
    asset->GetData(), ::testing::Each(static_cast<uint8_t>(fill_value)));
  EXPECT_EQ(asset->GetWidth(), 8u);
  EXPECT_EQ(asset->GetHeight(), 8u);
  EXPECT_EQ(asset->GetTextureType(), oxygen::TextureType::kTexture2D);

  // Optionally, check that the data at the offset is as expected
  // (if TextureResource exposes a way to access the raw data)
}

//! Test: LoadTextureResource handles zero size_bytes (no texture data)
//! gracefully.
NOLINT_TEST_F(TextureLoaderBasicTest, LoadTexture_ZeroDataSize_Works)
{
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), size_bytes =
  // 0
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 0           (00 00 00 00)
  //   0x0C: texture_type     = 1           (01)  // kTexture1D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 1           (01 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 2           (02)  // kR8SInt
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 00 00 00 00 01 00 10 00
    16: 00 00 01 00 00 00 01 00 01 00 01 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 0;
  constexpr auto fill_value = std::byte { 0x00 };

  // Write header only (no image data needed)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetData().size(), 0u);
  EXPECT_EQ(asset->GetWidth(), 16u);
  EXPECT_EQ(asset->GetHeight(), 1u); // Fixed expected value
  EXPECT_EQ(asset->GetTextureType(), oxygen::TextureType::kTexture1D);
}

//=== TextureLoader Error Handling Tests ===----------------------------------//

//! Fixture for TextureLoader error test cases.
class TextureLoaderErrorTest : public TextureLoaderBasicTest {
  // No additional members needed for now; extend as needed for error scenarios.
};

//! Test: LoadTextureResource throws if the header is truncated (less than 40
//! bytes).
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_TruncatedHeader_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Write only 16 bytes (less than the required 40 bytes for header)
  const std::string truncated_hexdump = R"(
     0: 00 01 00 00 00 00 00 00 1F 01 00 00 04 00 80 00
  )";

  // Write incomplete header, no image data
  WriteDescriptorWithData(
    desc_writer_, data_writer_, truncated_hexdump, 0, std::byte { 0x00 });

  // Act + Assert: Should throw due to incomplete header
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws for unsupported texture type.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_UnsupportedTextureType_Throws)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::content::testing::WriteDescriptorWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // texture_type = 255 (invalid), padded to 256 (to place the texture data
  // after)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 128         (80 00 00 00)
  //   0x0C: texture_type     = 255         (FF) <- invalid
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 1           (01 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 2           (02)  // kR8SInt
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 FF 00 10 00
    16: 00 00 01 00 00 00 01 00 01 00 01 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 128;
  constexpr auto fill_value = std::byte { 0x11 };

  // Write header and 128 bytes of data (simulate offset)
  WriteDescriptorWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act + Assert: Should throw due to unsupported texture type
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws when data read fails during texture data
//! loading.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_DataReadFailure_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Valid header but insufficient texture data
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 1024        (00 04 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 32          (20 00 00 00)
  //   0x12: height           = 32          (20 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 00 04 00 00 03 00 20 00
    16: 00 00 20 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t actual_data_size = 512; // But only provide 512 bytes

  // Write header and insufficient data (simulate read failure)
  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + actual_data_size, std::byte { 0xAA });

  // Act + Assert: Should throw due to insufficient data
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws when data_offset is beyond stream bounds.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_InvalidDataOffset_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Header with data_offset pointing beyond stream end
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 10000       (10 27 00 00 00 00 00 00)
  //   0x08: size_bytes       = 64          (40 00 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 8           (08 00 00 00)
  //   0x12: height           = 8           (08 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 10 27 00 00 00 00 00 00 40 00 00 00 03 00 08 00
    16: 00 00 08 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint32_t actual_stream_size = 300; // Much smaller than offset

  // Write header and small amount of data (offset points beyond this)
  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    actual_stream_size, std::byte { 0xBB });

  // Act + Assert: Should throw due to invalid offset
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws for zero width dimension.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_ZeroWidth_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Header with width = 0 (invalid)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 16          (10 00 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 0           (00 00 00 00) <- invalid
  //   0x12: height           = 16          (10 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 03 00 00 00
    16: 00 00 10 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;

  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + size_bytes, std::byte { 0xCC });

  // Act + Assert: Should throw due to zero width
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws for zero height dimension.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_ZeroHeight_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Header with height = 0 (invalid)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 16          (10 00 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 0           (00 00 00 00) <- invalid
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 03 00 10 00
    16: 00 00 00 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;

  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + size_bytes, std::byte { 0xDD });

  // Act + Assert: Should throw due to zero height
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws for zero depth dimension in 3D texture.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_ZeroDepth_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: 3D texture header with depth = 0 (invalid)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 16          (10 00 00 00)
  //   0x0C: texture_type     = 9           (09)  // kTexture3D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 8           (08 00 00 00)
  //   0x12: height           = 8           (08 00 00 00)
  //   0x16: depth            = 0           (00 00) <- invalid for 3D texture
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 09 00 08 00
    16: 00 00 08 00 00 00 00 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;

  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + size_bytes, std::byte { 0xEE });

  // Act + Assert: Should throw due to zero depth for 3D texture
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws for invalid dimension combination.
NOLINT_TEST_F(
  TextureLoaderErrorTest, LoadTexture_InvalidDimensionCombination_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: 1D texture with height > 1 (invalid combination)
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 16          (10 00 00 00)
  //   0x0C: texture_type     = 1           (01)  // kTexture1D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 16          (10 00 00 00) <- invalid for 1D texture
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 01 00 10 00
    16: 00 00 10 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;

  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + size_bytes, std::byte { 0xFF });

  // Act + Assert: Should throw due to invalid dimension combination
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

//! Test: LoadTextureResource throws when stream seek fails.
NOLINT_TEST_F(TextureLoaderErrorTest, LoadTexture_StreamSeekFailure_Throws)
{
  using oxygen::content::testing::WriteDescriptorWithData;

  // Arrange: Valid header but force seek failure by corrupting stream
  // Field layout:
  // clang-format off
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes       = 64          (40 00 00 00)
  //   0x0C: texture_type     = 3           (03)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 8           (08 00 00 00)
  //   0x12: height           = 8           (08 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 30          (1E)  // kRGBA8UNorm
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  // clang-format on
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 40 00 00 00 03 00 08 00
    16: 00 00 08 00 00 00 01 00 01 00 01 00 1E 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 64;

  WriteDescriptorWithData(desc_writer_, data_writer_, hexdump,
    data_offset + size_bytes, std::byte { 0x77 });

  // Act + Assert: Should throw due to seek failure
  const auto context = CreateLoaderContext();

  // Force seek failure by setting failure flag on data stream after context
  // creation
  data_stream_.ForceFail(true);

  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

} // namespace
