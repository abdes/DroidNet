//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "Fixtures/LoaderTestFixtures.h"
#include "Utils/PakUtils.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/SourceOrigin.h>
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

auto CopyTerminated(
  const std::string_view text, const std::span<char> destination) -> void
{
  std::ranges::fill(destination, '\0');
  std::ranges::copy(
    text.substr(0, destination.size() - 1U), destination.begin());
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

  static auto MakeMaterialDescriptor(const std::string_view name)
    -> MaterialAssetDesc
  {
    MaterialAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    CopyTerminated(name, desc.header.name);
    desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
    desc.material_domain
      = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
    return desc;
  }

  struct ShaderSource {
    std::string_view path;
    std::string_view entry_point;
    std::string_view defines;
  };

  static auto MakeShaderReferenceDesc(oxygen::ShaderType shader_type,
    const ShaderSource source, uint64_t hash) -> ShaderReferenceDesc
  {
    ShaderReferenceDesc desc {};
    desc.shader_type = static_cast<uint8_t>(shader_type);

    CopyTerminated(source.path, desc.source_path);
    CopyTerminated(source.entry_point, desc.entry_point);
    CopyTerminated(source.defines, desc.defines);

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
  constexpr auto kEmission = std::array {
    9.7F,
    0.00001F,
    oxygen::data::pak::render::kMaxMaterialEmissiveFactor,
  };
  std::ranges::copy(kEmission, std::begin(desc.emissive_factor));
  WriteMaterialDescriptor(desc);

  const auto material = LoadMaterialAsset(CreateLoaderContext());
  ASSERT_NE(material, nullptr);
  const auto emission = material->GetEmissiveFactor();
  for (size_t channel = 0; channel < emission.size(); ++channel) {
    EXPECT_EQ(emission.at(channel),
      CheckedAt(std::span(desc.emissive_factor), channel));
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
  constexpr uint32_t kFlags = 0xAABBCCDDU;
  constexpr uint32_t kShaderStages = 0x88U;
  constexpr auto kBaseColor = std::array { 0.1F, 0.2F, 0.3F, 0.4F };
  constexpr float kNormalScale = 1.5F;
  constexpr float kMetalness = 0.7F;
  constexpr float kRoughness = 0.2F;
  constexpr float kOcclusion = 0.9F;
  constexpr auto kTextureSlots = std::array { 42U, 43U, 44U, 45U, 46U };
  constexpr auto kUvScale = std::array { 2.0F, 3.0F };
  constexpr auto kUvOffset = std::array { 0.25F, 0.75F };
  desc.flags = kFlags;
  desc.shader_stages = kShaderStages;
  std::ranges::copy(kBaseColor, std::begin(desc.base_color));
  desc.normal_scale = kNormalScale;
  desc.metalness = oxygen::data::Unorm16(kMetalness);
  desc.roughness = oxygen::data::Unorm16(kRoughness);
  desc.ambient_occlusion = oxygen::data::Unorm16(kOcclusion);
  desc.base_color_texture
    = oxygen::data::ResourceReferenceIndex { kTextureSlots.at(0) };
  desc.normal_texture
    = oxygen::data::ResourceReferenceIndex { kTextureSlots.at(1) };
  desc.metallic_texture
    = oxygen::data::ResourceReferenceIndex { kTextureSlots.at(2) };
  desc.roughness_texture
    = oxygen::data::ResourceReferenceIndex { kTextureSlots.at(3) };
  desc.ambient_occlusion_texture
    = oxygen::data::ResourceReferenceIndex { kTextureSlots.at(4) };
  std::ranges::copy(kUvScale, std::begin(desc.uv_scale));
  std::ranges::copy(kUvOffset, std::begin(desc.uv_offset));
  desc.uv_rotation_radians = 0.5F;
  desc.uv_set = 1U;

  const std::array<ShaderReferenceDesc, 2> shader_descs {
    MakeShaderReferenceDesc(ShaderType::kVertex,
      {
        .path = "main.vert",
        .entry_point = "VS",
        .defines = "",
      },
      0x1111U),
    MakeShaderReferenceDesc(ShaderType::kPixel,
      {
        .path = "main.frag",
        .entry_point = "PS",
        .defines = "",
      },
      0x2222U),
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
                asset->GetBaseColorTexture().get(),
                asset->GetNormalTexture().get(),
                asset->GetMetallicTexture().get(),
                asset->GetRoughnessTexture().get(),
                asset->GetAmbientOcclusionTexture().get(),
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
  desc.base_color_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.normal_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.metallic_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.roughness_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.ambient_occlusion_texture = oxygen::data::ResourceReferenceIndex { 0U };
  WriteMaterialDescriptor(desc);

  // Act
  const auto references = oxygen::data::AssetReferences::Create(
    {
      {
        .kind = oxygen::data::ResourceKind::kTexture,
        .index = oxygen::ResourceIndexT { 0U },
      },
    },
    {});
  ASSERT_TRUE(references.has_value());
  auto [context, collector] = CreateDecodeLoaderContext();
  context.asset_references = oxygen::observer_ptr(&*references);
  auto asset = LoadMaterialAsset(std::move(context));

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(asset->GetAssetType(), AssetType::kMaterial);
  EXPECT_EQ(asset->GetBaseColorTexture().get(), 0U);
  EXPECT_EQ(asset->GetNormalTexture().get(), 0U);
  EXPECT_EQ(asset->GetMetallicTexture().get(), 0U);
  EXPECT_EQ(asset->GetRoughnessTexture().get(), 0U);
  EXPECT_EQ(asset->GetAmbientOcclusionTexture().get(), 0U);
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
    MakeShaderReferenceDesc(ShaderType::kVertex,
      {
        .path = "VertexShader",
        .entry_point = "VS",
        .defines = "",
      },
      0xBBAAU),
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
    constexpr size_t kTruncatedShaderSize = 50U;
    auto sh_buf
      = ParseHexDumpWithOffset(partial_shader_hexdump, kTruncatedShaderSize);
    ASSERT_TRUE(desc_writer_.WriteBlob(sh_buf));
  }
  EXPECT_TRUE(desc_stream_.Seek(0));

  // Act + Assert: Should throw due to incomplete shader data
  auto context = CreateLoaderContext();
  EXPECT_THROW({ (void)LoadMaterialAsset(context); }, std::runtime_error);
}

//! Repeated local texture bindings collect one exact source-qualified
//! dependency.
NOLINT_TEST_F(
  MaterialLoaderBasicTest, RepeatedTextureBindingCollectsOneExactDependency)
{
  using oxygen::content::internal::ResourceRef;
  using oxygen::data::TextureResource;

  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.base_color_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.normal_texture = oxygen::data::ResourceReferenceIndex { 0U };
  WriteMaterialDescriptor(desc);

  const auto references = oxygen::data::AssetReferences::Create(
    {
      {
        .kind = oxygen::data::ResourceKind::kTexture,
        .index = oxygen::ResourceIndexT { 42U },
      },
    },
    {});
  ASSERT_TRUE(references.has_value());
  auto [context, collector] = CreateDecodeLoaderContext();
  context.asset_references = oxygen::observer_ptr(&*references);
  (void)LoadMaterialAsset(std::move(context));

  const ResourceRef expected {
    .source = oxygen::data::SourceInstanceId { 7 },
    .resource_type_id = TextureResource::ClassTypeId(),
    .resource_index = oxygen::ResourceIndexT { 42U },
  };

  EXPECT_THAT(
    collector->ResourceRefDependencies(), ::testing::ElementsAre(expected));
}

} // namespace
