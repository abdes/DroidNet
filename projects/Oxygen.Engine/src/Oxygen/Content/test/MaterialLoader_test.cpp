//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "Fixtures/LoaderTestFixtures.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MaterialTextureSlot.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::SizeIs;

using oxygen::content::loaders::LoadMaterialAsset;

using oxygen::base::CheckedAt;

namespace {

using oxygen::cooker::testing::MaterialBuilder;
using oxygen::cooker::testing::MaterialRefusal;
using oxygen::data::CookedDefect;

//! Expects `load` to be refused with a `CookedFormatError` of `expected`.
auto ExpectDefect(auto&& load, const CookedDefect expected) -> void
{
  try {
    load();
    FAIL() << "The descriptor was accepted";
  } catch (const oxygen::data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), expected);
  }
}

//=== MaterialLoader Basic Functionality Tests ===----------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  using MaterialAssetDesc = oxygen::data::pak::render::MaterialAssetDesc;
  using ShaderReferenceDesc = oxygen::data::pak::render::ShaderReferenceDesc;

  //! Writes `desc`, its shader references and the fixture's string table.
  auto WriteMaterialDescriptor(MaterialAssetDesc desc,
    std::span<const ShaderReferenceDesc> shader_refs = {}) -> void
  {
    desc.header.strings = {
      .offset = static_cast<oxygen::data::pak::core::StringTableOffsetT>(
        sizeof(MaterialAssetDesc)
        + (shader_refs.size() * sizeof(ShaderReferenceDesc))),
      .size = strings_.Size(),
    };
    auto pack = desc_writer_.ScopedAlignment(1);
    ASSERT_TRUE(desc_writer_.WriteBlob(
      std::as_bytes(std::span<const MaterialAssetDesc, 1>(&desc, 1))));
    for (const ShaderReferenceDesc& ref : shader_refs) {
      ASSERT_TRUE(desc_writer_.WriteBlob(
        std::as_bytes(std::span<const ShaderReferenceDesc, 1>(&ref, 1))));
    }
    ASSERT_TRUE(desc_writer_.WriteBlob(strings_.Bytes()));
    EXPECT_TRUE(desc_stream_.Seek(oxygen::serio::ByteOffset { 0 }));
  }

  auto MakeMaterialDescriptor(const std::string_view name) -> MaterialAssetDesc
  {
    MaterialAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    desc.header.name_offset = strings_.Add(name);
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

  auto MakeShaderReferenceDesc(oxygen::ShaderType shader_type,
    const ShaderSource source, uint64_t hash) -> ShaderReferenceDesc
  {
    ShaderReferenceDesc desc {};
    desc.shader_type = static_cast<uint8_t>(shader_type);

    desc.source_path_offset = strings_.Add(source.path);
    desc.entry_point_offset = strings_.Add(source.entry_point);
    desc.defines_offset = strings_.Add(source.defines);

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

  //! The string table of the descriptor being built.
  oxygen::data::DescriptorStringsBuilder strings_;
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

  ExpectDefect([&] { (void)LoadMaterialAsset(CreateLoaderContext()); },
    CookedDefect::kUnsupportedVersion);
}

NOLINT_TEST_F(MaterialLoaderBasicTest, RejectsNonFiniteEmission)
{
  auto desc = MakeMaterialDescriptor("Invalid material");
  desc.emissive_factor[0] = std::numeric_limits<float>::infinity();
  WriteMaterialDescriptor(desc);

  ExpectDefect([&] { (void)LoadMaterialAsset(CreateLoaderContext()); },
    CookedDefect::kInvalidValue);
}

//! A slot's sampling must name a UV set and sampler that exist, and transform
//! by finite values.
NOLINT_TEST_F(MaterialLoaderBasicTest, RejectsInvalidTextureSampling)
{
  using oxygen::data::pak::render::TextureSlotSampling;
  const auto invalid = std::array {
    TextureSlotSampling { .uv_set = 2U },
    TextureSlotSampling { .address_u = 3U },
    TextureSlotSampling { .address_v = 3U },
    TextureSlotSampling { .mag_filter = 2U },
    TextureSlotSampling { .min_filter = 2U },
    TextureSlotSampling { .mip_filter = 3U },
    TextureSlotSampling {
      .uv_scale = { std::numeric_limits<float>::infinity(), 1.0F },
    },
    TextureSlotSampling {
      .uv_offset = { 0.0F, std::numeric_limits<float>::quiet_NaN() },
    },
    TextureSlotSampling {
      .uv_rotation_radians = std::numeric_limits<float>::infinity(),
    },
  };
  for (const auto& sampling : invalid) {
    ASSERT_TRUE(desc_stream_.Seek(oxygen::serio::ByteOffset { 0 }));
    auto desc = MakeMaterialDescriptor("Invalid sampling");
    desc.texture_sampling[3] = sampling;
    WriteMaterialDescriptor(desc);

    ExpectDefect([&] { (void)LoadMaterialAsset(CreateLoaderContext()); },
      CookedDefect::kInvalidValue);
  }
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
  constexpr uint32_t kFlags
    = oxygen::data::pak::render::kMaterialFlag_DoubleSided
    | oxygen::data::pak::render::kMaterialFlag_AlphaTest;
  constexpr uint32_t kShaderStages = 0x88U;
  constexpr auto kBaseColor = std::array { 0.1F, 0.2F, 0.3F, 0.4F };
  constexpr float kNormalScale = 1.5F;
  constexpr float kMetalness = 0.7F;
  constexpr float kRoughness = 0.2F;
  constexpr float kOcclusion = 0.9F;
  constexpr auto kTextureSlots = std::array { 42U, 43U, 44U, 45U, 46U };
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
  // The last slot, so the read stays in step across all of them.
  desc.texture_sampling[11] = oxygen::data::pak::render::TextureSlotSampling {
    .uv_scale = { 2.0F, 3.0F },
    .uv_offset = { 0.25F, 0.75F },
    .uv_rotation_radians = 0.5F,
    .uv_set = 1U,
    .address_u = 2U,
    .address_v = 1U,
    .mag_filter = 0U,
    .min_filter = 0U,
    .mip_filter = 0U,
  };

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
  EXPECT_EQ(asset->GetFlags(), kFlags);
  EXPECT_FLOAT_EQ(asset->GetNormalScale(), 1.5F);
  EXPECT_NEAR(asset->GetMetalness(), 0.7F, 1.0F / 65535.0F);
  EXPECT_NEAR(asset->GetRoughness(), 0.2F, 1.0F / 65535.0F);
  EXPECT_NEAR(asset->GetAmbientOcclusion(), 0.9F, 1.0F / 65535.0F);
  using oxygen::data::MaterialTextureSlot;
  const auto& sampling
    = asset->GetTextureSampling(MaterialTextureSlot::kThickness);
  EXPECT_FLOAT_EQ(sampling.uv_scale[0], 2.0F);
  EXPECT_FLOAT_EQ(sampling.uv_scale[1], 3.0F);
  EXPECT_FLOAT_EQ(sampling.uv_offset[0], 0.25F);
  EXPECT_FLOAT_EQ(sampling.uv_offset[1], 0.75F);
  EXPECT_FLOAT_EQ(sampling.uv_rotation_radians, 0.5F);
  EXPECT_EQ(sampling.uv_set, 1U);
  EXPECT_EQ(asset->GetTextureSamplerState(MaterialTextureSlot::kThickness),
    (oxygen::TextureSamplerState {
      .address_u = oxygen::TextureAddress::kClamp,
      .address_v = oxygen::TextureAddress::kMirror,
      .mag_filter = oxygen::TextureFilter::kNearest,
      .min_filter = oxygen::TextureFilter::kNearest,
      .mip_filter = oxygen::TextureMipFilter::kNone,
    }));
  EXPECT_EQ(asset->GetTextureSamplerState(MaterialTextureSlot::kBaseColor),
    oxygen::TextureSamplerState {});

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

//! A descriptor that ends inside its asset header is refused as truncated.
NOLINT_TEST(MaterialLoaderRefusalTest, RefusesADescriptorCutInsideItsHeader)
{
  auto bytes = MaterialBuilder { "material" }.Bytes();
  bytes.resize(sizeof(oxygen::data::pak::core::AssetHeader) / 2U);

  EXPECT_EQ(MaterialRefusal(bytes), CookedDefect::kTruncated);
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
  ASSERT_HAS_VALUE(references);
  auto [context, collector] = CreateDecodeLoaderContext();
  context.asset_references = oxygen::observer_ptr(&*references);
  auto asset = LoadMaterialAsset(context);

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

//! A descriptor cut inside its shader references is refused as truncated,
//! before they are read: it also ends before the string table that follows
//! them.
NOLINT_TEST(MaterialLoaderRefusalTest, RefusesADescriptorCutInsideItsShaders)
{
  auto bytes = MaterialBuilder { "material" }.PixelShader().Bytes();
  bytes.resize(sizeof(oxygen::data::pak::render::MaterialAssetDesc)
    + (sizeof(oxygen::data::pak::render::ShaderReferenceDesc) / 2U));

  EXPECT_EQ(MaterialRefusal(bytes), CookedDefect::kTruncated);
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
  ASSERT_HAS_VALUE(references);
  auto [context, collector] = CreateDecodeLoaderContext();
  context.asset_references = oxygen::observer_ptr(&*references);
  (void)LoadMaterialAsset(context);

  const ResourceRef expected {
    .source = oxygen::data::SourceInstanceId { 7 },
    .resource_type_id = TextureResource::ClassTypeId(),
    .resource_index = oxygen::ResourceIndexT { 42U },
  };

  EXPECT_THAT(
    collector->ResourceRefDependencies(), ::testing::ElementsAre(expected));
}

//! Sentinel texture indices (error texture, no texture) are skipped by the
//! dependency collector.
NOLINT_TEST_F(
  MaterialLoaderBasicTest, SentinelTextureIndicesCollectNoDependencies)
{
  // Arrange
  auto desc = MakeMaterialDescriptor("Test Material");
  desc.base_color_texture = oxygen::data::ResourceReferenceIndex { 0U };
  desc.emissive_texture = oxygen::data::ResourceReferenceIndex { 1U };
  WriteMaterialDescriptor(desc);

  const auto references = oxygen::data::AssetReferences::Create(
    {
      {
        .kind = oxygen::data::ResourceKind::kTexture,
        .index = oxygen::data::pak::core::kErrorTextureResourceIndex,
      },
      {
        .kind = oxygen::data::ResourceKind::kTexture,
        .index = oxygen::data::pak::core::kNoResourceIndex,
      },
    },
    {});
  ASSERT_HAS_VALUE(references);

  // Act
  auto [context, collector] = CreateDecodeLoaderContext();
  context.asset_references = oxygen::observer_ptr(&*references);
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
  EXPECT_EQ(
    asset->GetBaseColorTexture(), oxygen::data::ResourceReferenceIndex { 0U });
  EXPECT_EQ(
    asset->GetEmissiveTexture(), oxygen::data::ResourceReferenceIndex { 1U });
  EXPECT_EQ(asset->GetNormalTexture(), oxygen::data::kNoResourceReference);
  EXPECT_THAT(collector->ResourceRefDependencies(), SizeIs(0));
}

} // namespace
