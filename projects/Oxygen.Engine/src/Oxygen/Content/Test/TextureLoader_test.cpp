//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/Test/Mocks/MockStream.h>
#include <Oxygen/Data/TextureResource.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

using oxygen::content::loaders::LoadTextureResource;
using oxygen::serio::Reader;

namespace {

//=== TextureLoader Test Fixtures ===--------------------------------------//

//! Fixture for TextureLoader basic serialization tests.
class TextureLoaderBasicTestFixture : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  TextureLoaderBasicTestFixture()
    : writer(stream)
    , reader(stream)
  {
  }

  //! Helper method to create LoaderContext for testing.
  auto CreateLoaderContext() -> oxygen::content::LoaderContext<MockStream>
  {
    return oxygen::content::LoaderContext<MockStream> { .asset_loader
      = nullptr, // Resources don't use asset_loader
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .reader = std::ref(reader),
      .offline = false };
  }

  MockStream stream;
  Writer writer;
  Reader<MockStream> reader;
};

//! Fixture for TextureLoader error test cases.
class TextureLoaderErrorTestFixture : public TextureLoaderBasicTestFixture {
  // No additional members needed for now; extend as needed for error scenarios.
};

//=== TextureLoader Basic Functionality Tests ===-----------------------------//

//! Test: LoadTextureResource returns valid TextureResource for correct input.
NOLINT_TEST_F(
  TextureLoaderBasicTestFixture, LoadTexture_ValidInput_ReturnsTextureAsset)
{
  using oxygen::data::TextureResource;

  // Arrange

  using oxygen::data::pak::TextureResourceDesc;
  constexpr TextureResourceDesc desc {
    .data_offset = 256,
    .data_size = 287,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTextureCube),
    .compression_type = 0, // Example: 0 = uncompressed
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_layers = 6, // For cubemap
    .mip_levels = 5,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA32Float),
    .alignment = 256,
    .is_cubemap = 1,
    .reserved = { 0 },
  };

  // Write header (TextureResourceDesc)
  ASSERT_TRUE(writer.write(desc));
  if (desc.data_offset > sizeof(desc)) {
    std::vector<std::byte> pad(
      desc.data_offset - sizeof(desc), std::byte { 0 });
    ASSERT_TRUE(writer.write_blob(pad));
  }
  std::vector<std::byte> image_data(desc.data_size, std::byte(0x99));
  ASSERT_TRUE(writer.write_blob(image_data));
  stream.seek(0);

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadTextureResource(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetWidth(), 128u);
  EXPECT_EQ(asset->GetHeight(), 64u);
  EXPECT_EQ(asset->GetDepth(), 1u);
  EXPECT_EQ(asset->GetArrayLayers(), 6u);
  EXPECT_EQ(asset->GetMipCount(), 5u);
  EXPECT_EQ(asset->GetFormat(), oxygen::Format::kRGBA32Float);
  EXPECT_EQ(asset->GetDataSize(), 287u);
  EXPECT_EQ(asset->GetDataAlignment(), 256u);
  EXPECT_TRUE(asset->IsCubemap());
  EXPECT_EQ(asset->GetDataOffset(), 256u);
  EXPECT_EQ(asset->GetTextureType(), oxygen::TextureType::kTextureCube);
  EXPECT_EQ(asset->GetCompressionType(), 0u);
}

//! Test: LoadTextureResource returns kUnknown for invalid texture type.
NOLINT_TEST_F(
  TextureLoaderBasicTestFixture, LoadTexture_InvalidTextureType_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::data::TextureResource;
  using oxygen::data::pak::TextureResourceDesc;

  constexpr TextureResourceDesc desc {
    .data_offset = 256,
    .data_size = 128,
    .texture_type = 255, // Invalid
    .compression_type = 0,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = static_cast<uint8_t>(Format::kRGBA32Float),
    .alignment = 256,
    .is_cubemap = 0,
    .reserved = { 0 },
  };
  ASSERT_TRUE(writer.write(desc));
  if (desc.data_offset > sizeof(desc)) {
    std::vector<std::byte> pad(
      desc.data_offset - sizeof(desc), std::byte { 0 });
    ASSERT_TRUE(writer.write_blob(pad));
  }
  std::vector<std::byte> image_data(desc.data_size, std::byte(0x11));
  ASSERT_TRUE(writer.write_blob(image_data));
  stream.seek(0);

  auto context = CreateLoaderContext();
  auto asset = LoadTextureResource(context);
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetTextureType(), TextureType::kUnknown);
}

//! Test: LoadTextureResource returns kUnknown for invalid format.
NOLINT_TEST_F(
  TextureLoaderBasicTestFixture, LoadTexture_InvalidFormat_ReturnsUnknown)
{
  using oxygen::Format;
  using oxygen::TextureType;
  using oxygen::data::TextureResource;
  using oxygen::data::pak::TextureResourceDesc;

  constexpr TextureResourceDesc desc {
    .data_offset = 256,
    .data_size = 128,
    .texture_type = static_cast<uint8_t>(TextureType::kTexture2D),
    .compression_type = 0,
    .width = 16,
    .height = 16,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = 255, // Invalid
    .alignment = 256,
    .is_cubemap = 0,
    .reserved = { 0 },
  };
  ASSERT_TRUE(writer.write(desc));
  if (desc.data_offset > sizeof(desc)) {
    std::vector<std::byte> pad(
      desc.data_offset - sizeof(desc), std::byte { 0 });
    ASSERT_TRUE(writer.write_blob(pad));
  }
  std::vector<std::byte> image_data(desc.data_size, std::byte(0x22));
  ASSERT_TRUE(writer.write_blob(image_data));
  stream.seek(0);

  auto context = CreateLoaderContext();
  auto asset = LoadTextureResource(context);
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetFormat(), Format::kUnknown);
}

//=== TextureLoader Error Handling Tests ===----------------------------------//

//! Test: LoadTextureResource throws if header cannot be read.
NOLINT_TEST_F(
  TextureLoaderErrorTestFixture, LoadTexture_FailsToReadHeader_Throws)
{
  // Act + Assert
  auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadTextureResource(context); }, std::runtime_error);
}

} // namespace
