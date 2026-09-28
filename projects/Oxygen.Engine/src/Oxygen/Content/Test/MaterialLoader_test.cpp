//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Fixtures/LoaderTestFixtures.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::IsSupersetOf;
using ::testing::NotNull;
using ::testing::SizeIs;

using oxygen::content::loaders::LoadMaterialAsset;

using oxygen::base::CheckedAt;

namespace {

//=== Test Resource Loaders ===----------------------------------------------//

//! Test loader function for TextureResource
auto LoadTestTextureResource(const oxygen::content::LoaderContext& /*context*/)
  -> std::unique_ptr<oxygen::data::TextureResource>
{
  // Create a minimal TextureResource for testing
  oxygen::data::pak::core::TextureResourceDesc desc {};
  std::vector<uint8_t> data {};
  return std::make_unique<oxygen::data::TextureResource>(
    std::move(desc), std::move(data));
}

//=== MaterialLoader Basic Functionality Tests ===----------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  using MaterialAssetDesc = oxygen::data::pak::render::MaterialAssetDesc;
  using ShaderReferenceDesc = oxygen::data::pak::render::ShaderReferenceDesc;

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
    desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
    desc.material_domain
      = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
    return desc;
  }

  static auto MakeShaderReferenceDesc(oxygen::ShaderType shader_type,
    const char* source_path, const char* entry_point, const char* defines,
    uint64_t hash) -> ShaderReferenceDesc
  {
    ShaderReferenceDesc desc {};
    desc.shader_type = static_cast<uint8_t>(shader_type);

    std::memset(desc.source_path, 0, sizeof(desc.source_path));
    if (source_path != nullptr) {
      const auto src_len = std::strlen(source_path);
      const auto copy_len = (src_len < (sizeof(desc.source_path) - 1))
        ? src_len
        : (sizeof(desc.source_path) - 1);
      std::memcpy(desc.source_path, source_path, copy_len);
      desc.source_path[copy_len] = '\0';
    }

    std::memset(desc.entry_point, 0, sizeof(desc.entry_point));
    if (entry_point != nullptr) {
      const auto src_len = std::strlen(entry_point);
      const auto copy_len = (src_len < (sizeof(desc.entry_point) - 1))
        ? src_len
        : (sizeof(desc.entry_point) - 1);
      std::memcpy(desc.entry_point, entry_point, copy_len);
      desc.entry_point[copy_len] = '\0';
    }

    std::memset(desc.defines, 0, sizeof(desc.defines));
    if (defines != nullptr) {
      const auto src_len = std::strlen(defines);
      const auto copy_len = (src_len < (sizeof(desc.defines) - 1))
        ? src_len
        : (sizeof(desc.defines) - 1);
      std::memcpy(desc.defines, defines, copy_len);
      desc.defines[copy_len] = '\0';
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
    return MakeLoaderContext(true, true);
  }

  auto CreateDecodeLoaderContext() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    return MakeDecodeLoaderContext();
  }
};

NOLINT_TEST_F(MaterialLoaderBasicTest, PreservesFloat32Emission)
{
  auto desc = MakeMaterialDescriptor("HDR material");
  desc.emissive_factor[0] = 9.7F;
  desc.emissive_factor[1] = 0.00001F;
  desc.emissive_factor[2]
    = oxygen::data::pak::render::kMaxMaterialEmissiveFactor;
  WriteMaterialDescriptor(desc);

  const auto material = LoadMaterialAsset(CreateLoaderContext());
  ASSERT_NE(material, nullptr);
  const auto emission = material->GetEmissiveFactor();
  for (size_t channel = 0; channel < emission.size(); ++channel) {
    EXPECT_EQ(emission.at(channel), desc.emissive_factor[channel]);
  }
}

NOLINT_TEST_F(MaterialLoaderBasicTest, RejectsObsoleteMaterialVersion)
{
  auto desc = MakeMaterialDescriptor("Obsolete material");
  desc.header.version = 2;
  WriteMaterialDescriptor(desc);

  EXPECT_THROW(LoadMaterialAsset(CreateLoaderContext()), std::runtime_error);
}

NOLINT_TEST_F(MaterialLoaderBasicTest, RejectsNonFiniteEmission)
{
  auto desc = MakeMaterialDescriptor("Invalid material");
  desc.emissive_factor[0] = std::numeric_limits<float>::infinity();
  WriteMaterialDescriptor(desc);

  EXPECT_THROW(LoadMaterialAsset(CreateLoaderContext()), std::runtime_error);
}

//! Test: LoadMaterialAsset returns valid MaterialAsset for correct input.
/*!
  Scenario: Loads a MaterialAsset from a binary descriptor and shader reference
  using a hexdump, verifying all fields and shader references are parsed
  correctly.
*/
NOLINT_TEST_F(
  MaterialLoaderBasicTest, LoadMaterialValidInputReturnsMaterialAsset)
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
  desc.flags = 0xAABBCCDDU;
  desc.shader_stages = 0x88U;
  desc.base_color[0] = 0.1F;
  desc.base_color[1] = 0.2F;
  desc.base_color[2] = 0.3F;
  desc.base_color[3] = 0.4F;
  desc.normal_scale = 1.5F;
  desc.metalness = oxygen::data::Unorm16(0.7F);
  desc.roughness = oxygen::data::Unorm16(0.2F);
  desc.ambient_occlusion = oxygen::data::Unorm16(0.9F);
  desc.base_color_texture = oxygen::data::pak::core::ResourceIndexT { 42U };
  desc.normal_texture = oxygen::data::pak::core::ResourceIndexT { 43U };
  desc.metallic_texture = oxygen::data::pak::core::ResourceIndexT { 44U };
  desc.roughness_texture = oxygen::data::pak::core::ResourceIndexT { 45U };
  desc.ambient_occlusion_texture
    = oxygen::data::pak::core::ResourceIndexT { 46U };
  desc.uv_scale[0] = 2.0F;
  desc.uv_scale[1] = 3.0F;
  desc.uv_offset[0] = 0.25F;
  desc.uv_offset[1] = 0.75F;
  desc.uv_rotation_radians = 0.5F;
  desc.uv_set = 1U;

  const std::array<ShaderReferenceDesc, 2> shader_descs {
    MakeShaderReferenceDesc(
      ShaderType::kVertex, "main.vert", "VS", "", 0x1111U),
    MakeShaderReferenceDesc(ShaderType::kPixel, "main.frag", "PS", "", 0x2222U),
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
  EXPECT_EQ(asset->GetFlags(), 0xAABBCCDDU);
  EXPECT_FLOAT_EQ(asset->GetNormalScale(), 1.5F);
  EXPECT_NEAR(asset->GetMetalness(), 0.7F, 1.0F / 65535.0F);
  EXPECT_NEAR(asset->GetRoughness(), 0.2F, 1.0F / 65535.0F);
  EXPECT_NEAR(asset->GetAmbientOcclusion(), 0.9F, 1.0F / 65535.0F);
  EXPECT_THAT(asset->GetUvScale(), ::testing::ElementsAre(Eq(2.0F), Eq(3.0F)));
  EXPECT_THAT(
    asset->GetUvOffset(), ::testing::ElementsAre(Eq(0.25F), Eq(0.75F)));
  EXPECT_FLOAT_EQ(asset->GetUvRotationRadians(), 0.5F);
  EXPECT_EQ(asset->GetUvSet(), 1U);

  EXPECT_THAT(asset->GetBaseColor(),
    ::testing::Pointwise(
      ::testing::FloatEq(), std::array<float, 4> { 0.1F, 0.2F, 0.3F, 0.4F }));
  EXPECT_THAT((std::array<unsigned, 5> {
                static_cast<unsigned>(asset->GetBaseColorTexture()),
                static_cast<unsigned>(asset->GetNormalTexture()),
                static_cast<unsigned>(asset->GetMetallicTexture()),
                static_cast<unsigned>(asset->GetRoughnessTexture()),
                static_cast<unsigned>(asset->GetAmbientOcclusionTexture()),
              }),
    ElementsAre(42U, 43U, 44U, 45U, 46U));

  auto shaders = asset->GetShaders();
  ASSERT_THAT(shaders, SizeIs(2));
  //! Vertex shader reference: expect correct type, name, and hash.
  EXPECT_THAT(CheckedAt(shaders, 0),
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kVertex),
      Property(&oxygen::data::ShaderReference::GetSourcePath, Eq("main.vert")),
      Property(&oxygen::data::ShaderReference::GetEntryPoint, Eq("VS")),
      Property(&oxygen::data::ShaderReference::GetDefines, Eq("")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x1111U))));

  //! Pixel shader reference: expect correct type, name, and hash.
  EXPECT_THAT(CheckedAt(shaders, 1),
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kPixel),
      Property(&oxygen::data::ShaderReference::GetSourcePath, Eq("main.frag")),
      Property(&oxygen::data::ShaderReference::GetEntryPoint, Eq("PS")),
      Property(&oxygen::data::ShaderReference::GetDefines, Eq("")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x2222U))));
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
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterialTruncatedHeaderThrows)
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
  MaterialLoaderBasicTest, LoadMaterialZeroTextureIndicesNoDependencies)
{
  using oxygen::content::internal::ResourceRef;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::TextureResource;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0;
  desc.base_color[0] = 1.0F;
  desc.base_color[1] = 1.0F;
  desc.base_color[2] = 1.0F;
  desc.base_color[3] = 1.0F;
  desc.normal_scale = 1.0F;
  desc.metalness = oxygen::data::Unorm16(1.0F);
  desc.roughness = oxygen::data::Unorm16(1.0F);
  desc.ambient_occlusion = oxygen::data::Unorm16(1.0F);
  desc.base_color_texture = oxygen::data::pak::core::ResourceIndexT { 0U };
  desc.normal_texture = oxygen::data::pak::core::ResourceIndexT { 0U };
  desc.metallic_texture = oxygen::data::pak::core::ResourceIndexT { 0U };
  desc.roughness_texture = oxygen::data::pak::core::ResourceIndexT { 0U };
  desc.ambient_occlusion_texture
    = oxygen::data::pak::core::ResourceIndexT { 0U };
  WriteMaterialDescriptor(desc);

  // Act
  auto [context, collector] = CreateDecodeLoaderContext();
  auto asset = LoadMaterialAsset(std::move(context));

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetAssetType(), AssetType::kMaterial);
  EXPECT_EQ(asset->GetBaseColorTexture(), 0U);
  EXPECT_EQ(asset->GetNormalTexture(), 0U);
  EXPECT_EQ(asset->GetMetallicTexture(), 0U);
  EXPECT_EQ(asset->GetRoughnessTexture(), 0U);
  EXPECT_EQ(asset->GetAmbientOcclusionTexture(), 0U);
  EXPECT_THAT(asset->GetShaders(), SizeIs(0)); // No shaders

  EXPECT_THAT(collector->ResourceRefDependencies(), SizeIs(0));
}

//! Test: Non-parse-only loads require a dependency collector.
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterialNoCollectorThrows)
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
NOLINT_TEST_F(MaterialLoaderBasicTest, LoadMaterialSingleShaderStageWorks)
{
  using oxygen::ShaderType;
  using oxygen::data::AssetType;
  using ::testing::AllOf;
  using ::testing::Property;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0x8U;
  const std::array<ShaderReferenceDesc, 1> shader_descs {
    MakeShaderReferenceDesc(
      ShaderType::kVertex, "VertexShader", "VS", "", 0xBBAAU),
  };
  WriteMaterialDescriptor(desc, shader_descs);

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  auto shaders = asset->GetShaders();
  ASSERT_THAT(shaders, SizeIs(1));
  EXPECT_THAT(CheckedAt(shaders, 0),
    AllOf(Property(
            &oxygen::data::ShaderReference::GetShaderType, ShaderType::kVertex),
      Property(
        &oxygen::data::ShaderReference::GetSourcePath, Eq("VertexShader")),
      Property(&oxygen::data::ShaderReference::GetEntryPoint, Eq("VS")),
      Property(&oxygen::data::ShaderReference::GetDefines, Eq("")),
      Property(
        &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0xBBAAU))));
}

//! Test: LoadMaterialAsset throws when shader reading fails.
/*!
  Scenario: Tests error handling when shader_stages indicates shaders exist
  but reading the shader reference fails due to insufficient data.
*/
NOLINT_TEST_F(MaterialLoaderErrorTest, LoadMaterialShaderReadFailureThrows)
{
  using oxygen::content::testing::ParseHexDumpWithOffset;

  // Arrange: Material indicating 1 shader but insufficient data
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.shader_stages = 0x8U;

  // Incomplete shader data (needs 424 bytes but only provide 50)
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
  MaterialLoaderBasicTest, LoadMaterialNonZeroTextureCollectsDependency)
{
  using oxygen::content::internal::ResourceRef;
  using oxygen::data::TextureResource;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.base_color_texture = oxygen::data::pak::core::ResourceIndexT { 42U };
  WriteMaterialDescriptor(desc);

  auto [context, collector] = CreateDecodeLoaderContext();
  (void)LoadMaterialAsset(std::move(context));

  const ResourceRef expected {
    .source = oxygen::content::internal::SourceToken { 7 },
    .resource_type_id = TextureResource::ClassTypeId(),
    .resource_index = oxygen::data::pak::core::ResourceIndexT { 42U },
  };

  EXPECT_THAT(collector->ResourceRefDependencies(), IsSupersetOf({ expected }));
}

} // namespace
