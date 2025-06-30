//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/Test/Mocks/MockStream.h>
#include <Oxygen/Data/TextureAsset.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

using oxygen::content::loaders::LoadTextureAsset;
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

  MockStream stream;
  Writer writer;
  Reader<MockStream> reader;
};

//! Fixture for TextureLoader error test cases.
class TextureLoaderErrorTestFixture : public TextureLoaderBasicTestFixture {
  // No additional members needed for now; extend as needed for error scenarios.
};

//=== TextureLoader Basic Functionality Tests ===--------------------------//

//! Test: LoadTextureAsset returns valid TextureAsset for correct input.
NOLINT_TEST_F(
  TextureLoaderBasicTestFixture, LoadTexture_ValidInput_ReturnsTextureAsset)
{
  // Arrange
  using oxygen::data::TextureAsset;
  constexpr struct TextureAssetHeader {
    uint32_t width = 128;
    uint32_t height = 64;
    uint32_t mip_count = 5;
    uint32_t array_layers = 2;
    uint32_t format = 42;
    uint32_t image_size = 287;
    uint32_t alignment = 256;
    uint8_t is_cubemap = 1;
    uint8_t reserved[35] = {};
  } header;
  static_assert(sizeof(TextureAssetHeader) == 64);

  // Write header
  ASSERT_TRUE(writer.write(header));
  ASSERT_TRUE(writer.align_to(header.alignment));
  std::array<std::byte, header.image_size> image_data = { std::byte(0x99) };
  ASSERT_TRUE(writer.write_blob(image_data));
  stream.seek(0);

  // Act
  auto asset = LoadTextureAsset(reader);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetWidth(), 128u);
  EXPECT_EQ(asset->GetHeight(), 64u);
  EXPECT_EQ(asset->GetMipCount(), 5u);
  EXPECT_EQ(asset->GetArrayLayers(), 2u);
  EXPECT_EQ(asset->GetFormat(), 42u);
  EXPECT_EQ(asset->GetImageSize(), header.image_size);
  EXPECT_EQ(asset->GetAlignment(), 256u);
  EXPECT_TRUE(asset->IsCubemap());

  // padding to 256 is required for alignement
  ASSERT_LE(sizeof(header), 256u);
  EXPECT_EQ(asset->GetDataOffset(), header.alignment);
}

//=== TextureLoader Error Handling Tests ===-------------------------------//

//! Test: LoadTextureAsset throws if header cannot be read.
NOLINT_TEST_F(
  TextureLoaderErrorTestFixture, LoadTexture_FailsToReadHeader_Throws)
{
  // Act + Assert
  EXPECT_THROW(
    { (void)LoadTextureAsset(std::move(reader)); }, std::runtime_error);
}

} // namespace
