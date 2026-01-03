//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
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
  using MaterialAssetDesc = oxygen::data::pak::MaterialAssetDesc;
  using ShaderReferenceDesc = oxygen::data::pak::ShaderReferenceDesc;

  MaterialLoaderBasicTest()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  auto WriteMaterialDescriptor(const MaterialAssetDesc& desc,
    std::span<const ShaderReferenceDesc> shader_refs = {}) -> void
  {
    auto pack = desc_writer_.ScopedAlignment(1);
    ASSERT_TRUE(desc_writer_.WriteBlob(
      std::as_bytes(std::span<const MaterialAssetDesc, 1>(&desc, 1))));
    for (const ShaderReferenceDesc& ref : shader_refs) {
      ASSERT_TRUE(desc_writer_.WriteBlob(
        std::as_bytes(std::span<const ShaderReferenceDesc, 1>(&ref, 1))));
    }
    EXPECT_TRUE(desc_stream_.Seek(0));
  }

  static auto MakeMaterialDescriptor(const char* name) -> MaterialAssetDesc
  {
    MaterialAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    std::memset(desc.header.name, 0, sizeof(desc.header.name));
    if (name != nullptr) {
      const auto src_len = std::strlen(name);
      const auto copy_len = (src_len < (sizeof(desc.header.name) - 1))
        ? src_len
        : (sizeof(desc.header.name) - 1);
      std::memcpy(desc.header.name, name, copy_len);
      desc.header.name[copy_len] = '\0';
    }
    desc.header.version = 1;
    desc.material_domain
      = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
    return desc;
  }

  static auto MakeShaderReferenceDesc(const char* unique_id, uint64_t hash)
    -> ShaderReferenceDesc
  {
    ShaderReferenceDesc desc {};
    std::memset(desc.shader_unique_id, 0, sizeof(desc.shader_unique_id));
    if (unique_id != nullptr) {
      const auto src_len = std::strlen(unique_id);
      const auto copy_len = (src_len < (sizeof(desc.shader_unique_id) - 1))
        ? src_len
        : (sizeof(desc.shader_unique_id) - 1);
      std::memcpy(desc.shader_unique_id, unique_id, copy_len);
      desc.shader_unique_id[copy_len] = '\0';
    }
    desc.shader_hash = hash;
    return desc;
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
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using ::testing::AllOf;
  using ::testing::ElementsAre;
  using ::testing::Eq;
  using ::testing::NotNull;
  using ::testing::Property;
  using ::testing::SizeIs;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.flags = 0xAABBCCDDu;
  desc.shader_stages = 0x88u;
  desc.base_color[0] = 0.1f;
  desc.base_color[1] = 0.2f;
  desc.base_color[2] = 0.3f;
  desc.base_color[3] = 0.4f;
  desc.normal_scale = 1.5f;
  desc.metalness = oxygen::data::Unorm16(0.7f);
  desc.roughness = oxygen::data::Unorm16(0.2f);
  desc.ambient_occlusion = oxygen::data::Unorm16(0.9f);
  desc.base_color_texture = 42u;
  desc.normal_texture = 43u;
  desc.metallic_texture = 44u;
  desc.roughness_texture = 45u;
  desc.ambient_occlusion_texture = 46u;

  const std::array<ShaderReferenceDesc, 2> shader_descs {
    MakeShaderReferenceDesc("VS@main.vert", 0x1111u),
    MakeShaderReferenceDesc("PS@main.frag", 0x2222u),
  };
  WriteMaterialDescriptor(desc, shader_descs);

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
  EXPECT_NEAR(asset->GetMetalness(), 0.7f, 1.0f / 65535.0f);
  EXPECT_NEAR(asset->GetRoughness(), 0.2f, 1.0f / 65535.0f);
  EXPECT_NEAR(asset->GetAmbientOcclusion(), 0.9f, 1.0f / 65535.0f);

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
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::TextureResource;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0;
  desc.base_color[0] = 1.0f;
  desc.base_color[1] = 1.0f;
  desc.base_color[2] = 1.0f;
  desc.base_color[3] = 1.0f;
  desc.normal_scale = 1.0f;
  desc.metalness = oxygen::data::Unorm16(1.0f);
  desc.roughness = oxygen::data::Unorm16(1.0f);
  desc.ambient_occlusion = oxygen::data::Unorm16(1.0f);
  desc.base_color_texture = 0;
  desc.normal_texture = 0;
  desc.metallic_texture = 0;
  desc.roughness_texture = 0;
  desc.ambient_occlusion_texture = 0;
  WriteMaterialDescriptor(desc);

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
  auto desc = MakeMaterialDescriptor("Test Material");
  WriteMaterialDescriptor(desc);

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
  using oxygen::data::AssetType;
  using ::testing::AllOf;
  using ::testing::Property;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0x8u;
  const std::array<ShaderReferenceDesc, 1> shader_descs {
    MakeShaderReferenceDesc("VertexShader", 0xBBAAu),
  };
  WriteMaterialDescriptor(desc, shader_descs);

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
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0x8u;

  // Incomplete shader data (needs 216 bytes but only provide 50)
  const std::string partial_shader_hexdump = R"(
     0: 56 65 72 74 65 78 53 68 61 64 65 72 00 00 00 00
    16: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    32: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    48: 00 00
  )";

  {
    auto pack = desc_writer_.ScopedAlignment(1);
    ASSERT_TRUE(desc_writer_.WriteBlob(
      std::as_bytes(std::span<const MaterialAssetDesc, 1>(&desc, 1))));
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
  using oxygen::data::TextureResource;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.base_color_texture = 42u;
  WriteMaterialDescriptor(desc);

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
