//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/SourceToken.h>
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
using ::testing::NotNull;
using ::testing::SizeIs;

using oxygen::content::loaders::LoadMaterialAsset;
using oxygen::serio::Reader;

namespace {

//=== Test Resource Loaders ===----------------------------------------------//

//! Test loader function for TextureResource
auto LoadTestTextureResource(const oxygen::content::LoaderContext& /*context*/)
  -> std::unique_ptr<oxygen::data::TextureResource>
{
  // Create a minimal TextureResource for testing
  oxygen::data::pak::TextureResourceDesc desc {};
  std::vector<uint8_t> data {};
  return std::make_unique<oxygen::data::TextureResource>(
    std::move(desc), std::move(data));
}

//=== MaterialLoader Basic Functionality Tests ===----------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  MaterialLoaderBasicTest()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  //! Helper method to create LoaderContext for testing.
  /*!
    NOTE: This creates a context for parse-only testing without requiring a
    mounted content source. Dependency loading/registration is skipped.
  */
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
      .work_offline = true,
      .parse_only = true,
    };
  }

  auto CreateDecodeLoaderContext() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    if (!desc_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek data_stream");
    }

    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();

    oxygen::content::LoaderContext context {
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .source_token = oxygen::content::internal::SourceToken(7U),
      .desc_reader = &desc_reader_,
      .data_readers = std::make_tuple(&data_reader_, &data_reader_),
      .work_offline = true,
      .dependency_collector = collector,
      .source_pak = nullptr,
      .parse_only = false,
    };
    return { std::move(context), std::move(collector) };
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

//=== MaterialLoader Error Handling Tests ===---------------------------------//

//! Fixture for MaterialLoader error test cases.
class MaterialLoaderErrorTest : public MaterialLoaderBasicTest {
  // Inherits all functionality from MaterialLoaderBasicTest
};

//! Test: LoadMaterialAsset throws when header reading fails.
/*!
  Scenario: Tests error handling when the material descriptor header is
  truncated or corrupted, ensuring proper error propagation.
*/
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterial_TruncatedHeader_Throws)
{
  using oxygen::content::testing::ParseHexDumpWithOffset;

  // Arrange: Write only partial header (insufficient bytes)
  const std::string truncated_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto buf = ParseHexDumpWithOffset(truncated_hexdump, 32);
    ASSERT_TRUE(desc_writer_.WriteBlob(buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act + Assert: Should throw due to incomplete header
  auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadMaterialAsset(context); }, std::runtime_error);
}

//! Test: LoadMaterialAsset handles zero texture indices correctly.
/*!
  Scenario: Tests material loading with all texture indices set to zero,
  verifying no resource dependencies are registered.
*/
NOLINT_TEST_F(
  MaterialLoaderBasicTest, LoadMaterial_ZeroTextureIndices_NoDependencies)
{
  using oxygen::content::internal::ResourceRef;
  using oxygen::content::testing::ParseHexDumpWithOffset;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::TextureResource;

  // Arrange: Material with all texture indices = 0
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
  //   0x60: flags                       = 0           (00 00 00 00)
  //   0x64: shader_stages               = 0           (00 00 00 00)
  //   0x68: base_color[0]               = 1.0f        (00 00 80 3F)
  //   0x6C: base_color[1]               = 1.0f        (00 00 80 3F)
  //   0x70: base_color[2]               = 1.0f        (00 00 80 3F)
  //   0x74: base_color[3]               = 1.0f        (00 00 80 3F)
  //   0x78: normal_scale                = 1.0f        (00 00 80 3F)
  //   0x7C: metalness                   = 1.0f        (00 00 80 3F)
  //   0x80: roughness                   = 1.0f        (00 00 80 3F)
  //   0x84: ambient_occlusion           = 1.0f        (00 00 80 3F)
  //   0x88: base_color_texture          = 0           (00 00 00 00)
  //   0x8C: normal_texture              = 0           (00 00 00 00)
  //   0x90: metallic_texture            = 0           (00 00 00 00)
  //   0x94: roughness_texture           = 0           (00 00 00 00)
  //   0x98: ambient_occlusion_texture   = 0           (00 00 00 00)
  //   0x9C: reserved_textures[8]        = {0}
  //   0xBC: reserved[68]                = {0}
  //   0xFF: (end of MaterialAssetDesc, no shader references follow)
  // clang-format on
  const std::string material_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
    96: 00 00 00 00 00 00 00 00 00 00 80 3F 00 00 80 3F
   112: 00 00 80 3F 00 00 80 3F 00 00 80 3F 00 00 80 3F
   128: 00 00 80 3F 00 00 80 3F 00 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act
  auto [context, collector] = CreateDecodeLoaderContext();
  auto asset = LoadMaterialAsset(std::move(context));

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetAssetType(), AssetType::kMaterial);
  EXPECT_EQ(asset->GetBaseColorTexture(), 0u);
  EXPECT_EQ(asset->GetNormalTexture(), 0u);
  EXPECT_EQ(asset->GetMetallicTexture(), 0u);
  EXPECT_EQ(asset->GetRoughnessTexture(), 0u);
  EXPECT_EQ(asset->GetAmbientOcclusionTexture(), 0u);
  EXPECT_THAT(asset->GetShaders(), SizeIs(0)); // No shaders

  EXPECT_THAT(collector->ResourceRefDependencies(), SizeIs(0));
}

//! Test: Non-parse-only loads require a dependency collector.
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterial_NoCollector_Throws)
{
  using oxygen::content::testing::ParseHexDumpWithOffset;

  const std::string material_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
    96: 00 00 00 00 00 00 00 00 00 00 80 3F 00 00 80 3F
   112: 00 00 80 3F 00 00 80 3F 00 00 80 3F 00 00 80 3F
   128: 00 00 80 3F 00 00 80 3F 2A 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  auto context = CreateLoaderContext();
  context.parse_only = false;
  context.dependency_collector.reset();

  EXPECT_THROW(
    { (void)LoadMaterialAsset(std::move(context)); }, std::runtime_error);
}

//! Test: LoadMaterialAsset handles single shader stage correctly.
/*!
  Scenario: Tests material loading with only one shader stage bit set,
  verifying correct shader parsing and popcount calculation.
*/
NOLINT_TEST_F(MaterialLoaderBasicTest, LoadMaterial_SingleShaderStage_Works)
{
  using oxygen::ShaderType;
  using oxygen::content::testing::ParseHexDumpWithOffset;
  using oxygen::data::AssetType;
  using ::testing::AllOf;
  using ::testing::Property;

  // Arrange: Material with only vertex shader (bit 3 set)
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
  //   0x60: flags                       = 0           (00 00 00 00)
  //   0x64: shader_stages               = 0x8         (08 00 00 00)
  //   0x68: base_color[0]               = 1.0f        (00 00 80 3F)
  //   0x6C: base_color[1]               = 1.0f        (00 00 80 3F)
  //   0x70: base_color[2]               = 1.0f        (00 00 80 3F)
  //   0x74: base_color[3]               = 1.0f        (00 00 80 3F)
  //   0x78: normal_scale                = 1.0f        (00 00 80 3F)
  //   0x7C: metalness                   = 1.0f        (00 00 80 3F)
  //   0x80: roughness                   = 1.0f        (00 00 80 3F)
  //   0x84: ambient_occlusion           = 1.0f        (00 00 80 3F)
  //   0x88: base_color_texture          = 0           (00 00 00 00)
  //   0x8C: normal_texture              = 0           (00 00 00 00)
  //   0x90: metallic_texture            = 0           (00 00 00 00)
  //   0x94: roughness_texture           = 0           (00 00 00 00)
  //   0x98: ambient_occlusion_texture   = 0           (00 00 00 00)
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
    96: 00 00 00 00 08 00 00 00 00 00 80 3F 00 00 80 3F
   112: 00 00 80 3F 00 00 80 3F 00 00 80 3F 00 00 80 3F
   128: 00 00 80 3F 00 00 80 3F 00 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";

  // ShaderReferenceDesc: VertexShader, hash=0xBBAA
  //   0x00: name = "VertexShader"
  //   0xC0: hash = AA BB 00 00 00 00 00 00
  const std::string shader_hexdump = R"(
     0: 56 65 72 74 65 78 53 68 61 64 65 72 00 00 00 00
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
   192: AA BB 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
    auto sh_buf = ParseHexDumpWithOffset(shader_hexdump, 216);
    ASSERT_TRUE(desc_writer_.WriteBlob(sh_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  auto shaders = asset->GetShaders();
  ASSERT_THAT(shaders, SizeIs(1));
  EXPECT_THAT(shaders[0],
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kVertex),
      Property(
        &oxygen::data::ShaderReference::GetShaderUniqueId, Eq("VertexShader")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0xBBAAu))));
}

//! Test: LoadMaterialAsset throws when shader reading fails.
/*!
  Scenario: Tests error handling when shader_stages indicates shaders exist
  but reading the shader reference fails due to insufficient data.
*/
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterial_ShaderReadFailure_Throws)
{
  using oxygen::content::testing::ParseHexDumpWithOffset;

  // Arrange: Material indicating 1 shader but insufficient data
  const std::string material_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
    96: 00 00 00 00 08 00 00 00 00 00 80 3F 00 00 80 3F
   112: 00 00 80 3F 00 00 80 3F 00 00 80 3F 00 00 80 3F
   128: 00 00 80 3F 00 00 80 3F 00 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";

  // Incomplete shader data (needs 216 bytes but only provide 50)
  const std::string partial_shader_hexdump = R"(
     0: 56 65 72 74 65 78 53 68 61 64 65 72 00 00 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
    auto sh_buf = ParseHexDumpWithOffset(partial_shader_hexdump, 50);
    ASSERT_TRUE(desc_writer_.WriteBlob(sh_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act + Assert: Should throw due to incomplete shader data
  auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadMaterialAsset(context); }, std::runtime_error);
}

//! Test: Non-zero texture indices are collected as ResourceRef dependencies.
NOLINT_TEST_F(
  MaterialLoaderBasicTest, LoadMaterial_NonZeroTexture_CollectsDependency)
{
  using oxygen::content::internal::ResourceRef;
  using oxygen::content::testing::ParseHexDumpWithOffset;
  using oxygen::data::TextureResource;

  // Arrange: Create minimal material with a single non-zero texture index
  // Set base_color_texture = 42 (at offset 0x88)
  const std::string material_hexdump = R"(
     0: 01 54 65 73 74 20 4D 61 74 65 72 69 61 6C 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    64: 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01
    96: 00 00 00 00 00 00 00 00 00 00 80 3F 00 00 80 3F
   112: 00 00 80 3F 00 00 80 3F 00 00 80 3F 00 00 80 3F
   128: 00 00 80 3F 00 00 80 3F 2A 00 00 00 00 00 00 00
   144: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   176: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   192: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   208: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   224: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
   240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    auto mat_buf = ParseHexDumpWithOffset(material_hexdump, 256);
    ASSERT_TRUE(desc_writer_.WriteBlob(mat_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  auto [context, collector] = CreateDecodeLoaderContext();
  (void)LoadMaterialAsset(std::move(context));

  const ResourceRef expected {
    .source = oxygen::content::internal::SourceToken { 7 },
    .resource_type_id = TextureResource::ClassTypeId(),
    .resource_index = 42,
  };

  EXPECT_THAT(collector->ResourceRefDependencies(), IsSupersetOf({ expected }));
}

} // namespace
