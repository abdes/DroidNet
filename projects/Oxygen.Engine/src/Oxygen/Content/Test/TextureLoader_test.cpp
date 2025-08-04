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
      .offline = false,
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
  using oxygen::content::testing::WriteTextureDescWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a valid TextureResourceDesc header (40 bytes), padded
  // to 256 (to place the texture data after)
  // Field layout:
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes        = 287         (1F 01 00 00)
  //   0x0C: texture_type     = 4           (04)  // kTextureCube
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 128         (80 00 00 00)
  //   0x12: height           = 64          (40 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 6           (06 00)
  //   0x1A: mip_levels       = 5           (05 00)
  //   0x1C: format           = 2           (02)  // kRGBA32Float
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 1F 01 00 00 04 00 80 00
    16: 00 00 40 00 00 00 01 00 06 00 05 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 287;
  constexpr auto fill_value = std::byte { 0x99 };

  // Write header and 287 bytes of data (simulate offset)
  WriteTextureDescWithData(
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

//! Test: LoadTextureResource returns kUnknown for invalid texture type.
NOLINT_TEST_F(
  TextureLoaderBasicTest, LoadTexture_InvalidTextureType_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::content::testing::WriteTextureDescWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // texture_type = 255 (invalid), padded to 256 (to place the texture data
  // after) Field layout:
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes        = 128         (80 00 00 00)
  //   0x0C: texture_type     = 255         (FF) <- invalid
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 16          (10 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 2           (02)  // kRGBA32Float
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 FF 00 10 00
    16: 00 00 10 00 00 00 01 00 01 00 01 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 128;
  constexpr auto fill_value = std::byte { 0x11 };

  // Write header and 128 bytes of data (simulate offset)
  WriteTextureDescWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetTextureType(), TextureType::kUnknown);
}

//! Test: LoadTextureResource returns kUnknown for invalid format.
NOLINT_TEST_F(TextureLoaderBasicTest, LoadTexture_InvalidFormat_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::content::testing::WriteTextureDescWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // format = 255 (invalid), padded to 256 (to place the texture data after)
  // Field layout:
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes        = 128         (80 00 00 00)
  //   0x0C: texture_type     = 1           (01)  // kTexture2D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 16          (10 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 255         (FF) <- invalid
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 80 00 00 00 01 00 10 00
    16: 00 00 10 00 00 00 01 00 01 00 01 00 FF 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 128;
  constexpr auto fill_value = std::byte { 0x22 };

  // Write header and 128 bytes of data (simulate offset)
  WriteTextureDescWithData(
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
  using oxygen::content::testing::WriteTextureDescWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), with
  // data_offset = 256 and size_bytes = 16. Field layout:
  //   0x00: data_offset      = 256         (40 00 00 00 00 00 00 00)
  //   0x08: size_bytes        = 16          (10 00 00 00)
  //   0x0C: texture_type     = 3           (03)
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 8           (08 00 00 00)
  //   0x12: height           = 8           (08 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 6           (06)
  //   0x1D: alignment        = 256         (01 00)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 10 00 00 00 03 00 08 00
    16: 00 00 08 00 00 00 01 00 01 00 01 00 06 00 01 00
    32: 00 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 16;
  constexpr auto fill_value = std::byte { 0x5A };

  // Write header and 272 bytes of data (simulate offset)
  WriteTextureDescWithData(
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
  using oxygen::content::testing::WriteTextureDescWithData;
  using oxygen::data::TextureResource;

  // Arrange: Hexdump for a TextureResourceDesc header (40 bytes), size_bytes =
  // 0 Field layout:
  //   0x00: data_offset      = 256         (00 01 00 00 00 00 00 00)
  //   0x08: size_bytes        = 0           (00 00 00 00)
  //   0x0C: texture_type     = 1           (01)  // kTexture1D
  //   0x0D: compression_type = 0           (00)
  //   0x0E: width            = 16          (10 00 00 00)
  //   0x12: height           = 16          (10 00 00 00)
  //   0x16: depth            = 1           (01 00)
  //   0x18: array_layers     = 1           (01 00)
  //   0x1A: mip_levels       = 1           (01 00)
  //   0x1C: format           = 2           (02)
  //   0x1D: alignment        = 256         (00 01)
  //   0x1F: reserved[9]      = {0}         (00 00 00 00 00 00 00 00 00)
  const std::string hexdump = R"(
     0: 00 01 00 00 00 00 00 00 00 00 00 00 01 00 10 00
    16: 00 00 10 00 00 00 01 00 01 00 01 00 02 00 01 00
    32: 00 00 00 00 00 00 00 00
  )";
  constexpr uint64_t data_offset = 256;
  constexpr uint32_t size_bytes = 0;
  constexpr auto fill_value = std::byte { 0x00 };

  // Write header only (no image data needed)
  WriteTextureDescWithData(
    desc_writer_, data_writer_, hexdump, data_offset + size_bytes, fill_value);

  // Act
  const auto context = CreateLoaderContext();
  const auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetData().size(), 0u);
  EXPECT_EQ(asset->GetWidth(), 16u);
  EXPECT_EQ(asset->GetHeight(), 16u);
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
  using oxygen::content::testing::WriteTextureDescWithData;

  // Arrange: Write only 16 bytes (less than the required 40 bytes for header)
  const std::string truncated_hexdump = R"(
     0: 00 01 00 00 00 00 00 00 1F 01 00 00 04 00 80 00
  )";

  // Write incomplete header, no image data
  WriteTextureDescWithData(
    desc_writer_, data_writer_, truncated_hexdump, 0, std::byte { 0x00 });

  // Act + Assert: Should throw due to incomplete header
  const auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

} // namespace
