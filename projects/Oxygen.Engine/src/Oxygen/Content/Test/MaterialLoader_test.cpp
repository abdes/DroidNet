//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

#include "Mocks/MockStream.h"
#include "Utils/PakUtils.h"

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::IsSupersetOf;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::SizeIs;

using oxygen::content::loaders::LoadMaterialAsset;
using oxygen::serio::Reader;

namespace {

//=== Mock AssetLoader ===----------------------------------------------------//

//! Mock AssetLoader for lightweight testing without PAK file dependencies.
class MockAssetLoader : public oxygen::content::AssetLoader {
public:
  MOCK_METHOD(void, AddAssetDependency,
    (const oxygen::data::AssetKey&, const oxygen::data::AssetKey&), (override));
  MOCK_METHOD(void, AddResourceDependency,
    (const oxygen::data::AssetKey&, oxygen::content::ResourceKey), (override));
};

//=== MaterialLoader Basic Functionality Tests ===----------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;
  NiceMock<MockAssetLoader> asset_loader;

  MaterialLoaderBasicTest()
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
      .asset_loader = &asset_loader,
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .desc_reader = &desc_reader_,
      .data_readers = std::make_tuple(&data_reader_, &data_reader_),
      .offline = true,
    };
  }

  MockStream desc_stream_;
  MockStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader<MockStream> desc_reader_;
  Reader<MockStream> data_reader_;
};

//! Test: LoadMaterialAsset returns valid MaterialAsset for correct input.
/*!
  Scenario: Loads a MaterialAsset from a binary descriptor and shader reference
  using a hexdump, verifying all fields and shader references are parsed
  correctly.
*/
NOLINT_TEST_F(
  MaterialLoaderBasicTest, LoadMaterial_ValidInput_ReturnsMaterialAsset)
{
  using oxygen::ShaderType;
  using oxygen::content::testing::ParseHexDumpWithOffset;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using ::testing::AllOf;
  using ::testing::ElementsAre;
  using ::testing::Eq;
  using ::testing::NotNull;
  using ::testing::Property;
  using ::testing::SizeIs;

  // Arrange
  // clang-format off
  // material_hexdump: MaterialAssetDesc (256 bytes)
  // Field layout:
  //   0x00: header.asset_type           = 1           (01)
  //   0x01: header.name                 = "Test Material" (54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00 ...)
  //   0x41: header.version              = 1           (01)
  //   0x42: header.streaming_priority   = 0           (00)
  //   0x43: header.content_hash         = 0           (00 00 00 00 00 00 00 00)
  //   0x4B: header.variant_flags        = 0           (00 00 00 00)
  //   0x4F: header.reserved[16]         = {0}
  //   0x5F: material_domain             = 1           (01)
  //   0x60: flags                       = 0xAABBCCDD  (DD CC BB AA)
  //   0x64: shader_stages               = 0x8 | 0x80  (88 00 00 00)
  //   0x68: base_color[0]               = 0.1f        (CC CC CC 3D)
  //   0x6C: base_color[1]               = 0.2f        (CD CC 4C 3E)
  //   0x70: base_color[2]               = 0.3f        (9A 99 99 3E)
  //   0x74: base_color[3]               = 0.4f        (CD CC CC 3E)
  //   0x78: normal_scale                = 1.5f        (00 00 C0 3F)
  //   0x7C: metalness                   = 0.7f        (33 33 33 3F)
  //   0x80: roughness                   = 0.2f        (CD CC 4C 3E)
  //   0x84: ambient_occlusion           = 0.9f        (66 66 66 3F)
  //   0x88: base_color_texture          = 42          (2A 00 00 00)
  //   0x8C: normal_texture              = 43          (2B 00 00 00)
  //   0x90: metallic_texture            = 44          (2C 00 00 00)
  //   0x94: roughness_texture           = 45          (2D 00 00 00)
  //   0x98: ambient_occlusion_texture   = 46          (2E 00 00 00)
  //   0x9C: reserved_textures[8]        = {0}
  //   0xBC: reserved[68]                = {0}
  //   0xFF: (end of MaterialAssetDesc, followed by ShaderReferenceDesc array)
  // clang-format on
  const std::string material_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
    96: DD CC BB AA 88 00 00 00 CC CC CC 3D CD CC 4C 3E
   112: 9A 99 99 3E CD CC CC 3E 00 00 C0 3F 33 33 33 3F
   128: CD CC 4C 3E 66 66 66 3F 2A 00 00 00 2B 00 00 00
   144: 2C 00 00 00 2D 00 00 00 2E 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";
  // Patch in the actual field values at the correct offsets:
  // (You can use a helper or do this in your test setup, but here's the raw hex
  // for the fields) Offsets are relative to the start of the descriptor (0x00).
  // 0x5F: material_domain = 01
  // 0x60: flags = DD CC BB AA
  // 0x64: shader_stages = 03 00 00 00
  // 0x68: base_color[0] = CD CC CC 3D
  // 0x6C: base_color[1] = 9A 99 99 3E
  // 0x70: base_color[2] = 9A 99 99 3E
  // 0x74: base_color[3] = CD CC CC 3E
  // 0x78: normal_scale = 00 00 C0 3F
  // 0x7C: metalness = 9A 99 39 3F
  // 0x80: roughness = CD CC CC 3D
  // 0x84: ambient_occlusion = 9A 99 73 3F
  // 0x88: base_color_texture = 2A 00 00 00
  // 0x8C: normal_texture = 2B 00 00 00
  // 0x90: metallic_texture = 2C 00 00 00
  // 0x94: roughness_texture = 2D 00 00 00
  // 0x98: ambient_occlusion_texture = 2E 00 00 00

  // ShaderReferenceDesc 1: VS@main.vert, hash=0x1111
  //   0x00: name = "VS@main.vert"
  //   0x80: hash = 11 11 00 00 00 00 00 00
  const std::string shader1_hexdump = R"(
     0: 56 53 40 6D 61 69 6E 2E 76 65 72 74 00 00 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    96: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   112: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   128: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 11 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00
  )";

  // ShaderReferenceDesc 2: PS@main.frag, hash=0x2222
  //   0x00: name = "PS@main.frag"
  //   0x80: hash = 22 22 00 00 00 00 00 00
  const std::string shader2_hexdump = R"(
     0: 50 53 40 6D 61 69 6E 2E 66 72 61 67 00 00 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    96: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   112: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   128: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 22 22 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
    auto sh1_buf = ParseHexDumpWithOffset(shader1_hexdump, 216);
    ASSERT_TRUE(desc_writer_.WriteBlob(sh1_buf));
    auto sh2_buf = ParseHexDumpWithOffset(shader2_hexdump, 216);
    ASSERT_TRUE(desc_writer_.WriteBlob(sh2_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act

  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert

  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetAssetType(), AssetType::kMaterial);
  EXPECT_EQ(asset->GetAssetName(), "Test Material");
  EXPECT_EQ(asset->GetMaterialDomain(), MaterialDomain::kOpaque);
  EXPECT_EQ(asset->GetFlags(), 0xAABBCCDDu);
  EXPECT_FLOAT_EQ(asset->GetNormalScale(), 1.5f);
  EXPECT_FLOAT_EQ(asset->GetMetalness(), 0.7f);
  EXPECT_FLOAT_EQ(asset->GetRoughness(), 0.2f);
  EXPECT_FLOAT_EQ(asset->GetAmbientOcclusion(), 0.9f);

  EXPECT_THAT(asset->GetBaseColor(),
    ::testing::Pointwise(
      ::testing::FloatEq(), std::array<float, 4> { 0.1f, 0.2f, 0.3f, 0.4f }));
  EXPECT_THAT((std::array<unsigned, 5> {
                static_cast<unsigned>(asset->GetBaseColorTexture()),
                static_cast<unsigned>(asset->GetNormalTexture()),
                static_cast<unsigned>(asset->GetMetallicTexture()),
                static_cast<unsigned>(asset->GetRoughnessTexture()),
                static_cast<unsigned>(asset->GetAmbientOcclusionTexture()),
              }),
    ElementsAre(42u, 43u, 44u, 45u, 46u));

  auto shaders = asset->GetShaders();
  ASSERT_THAT(shaders, SizeIs(2));
  //! Vertex shader reference: expect correct type, name, and hash.
  EXPECT_THAT(shaders[0],
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kVertex),
      Property(
        &oxygen::data::ShaderReference::GetShaderUniqueId, Eq("VS@main.vert")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x1111u))));

  //! Pixel shader reference: expect correct type, name, and hash.
  EXPECT_THAT(shaders[1],
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kPixel),
      Property(
        &oxygen::data::ShaderReference::GetShaderUniqueId, Eq("PS@main.frag")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x2222u))));
}

} // namespace
