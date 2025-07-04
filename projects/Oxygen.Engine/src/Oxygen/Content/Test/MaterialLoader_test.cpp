//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Base/Writer.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Test/Mocks/MockStream.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Testing/GTest.h>

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

//=== Mock AssetLoader
//===------------------------------------------------------//

//! Mock AssetLoader for lightweight testing without PAK file dependencies.
class MockAssetLoader : public oxygen::content::AssetLoader {
public:
  MOCK_METHOD(void, AddAssetDependency,
    (const oxygen::data::AssetKey&, const oxygen::data::AssetKey&), (override));
  MOCK_METHOD(void, AddResourceDependency,
    (const oxygen::data::AssetKey&, oxygen::content::ResourceKey), (override));
};

//=== MaterialLoader Test Fixtures ===-------------------------------------//

//! Fixture for MaterialLoader basic serialization tests.
class MaterialLoaderBasicTestFixture : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;

  MaterialLoaderBasicTestFixture()
    : writer(stream)
    , reader(stream)
  {
  }

  //! Helper method to write MaterialAssetDesc using write_blob for arrays.
  auto WriteMaterialAssetDesc(const oxygen::data::pak::MaterialAssetDesc& desc)
    -> bool
  {
    auto pack = writer.ScopedAlignement(1);
    // Write AssetHeader field-by-field
    if (!writer.write(desc.header.asset_type))
      return false;
    if (!writer.write_blob(std::as_bytes(std::span(desc.header.name))))
      return false;
    if (!writer.write(desc.header.version))
      return false;
    if (!writer.write(desc.header.streaming_priority))
      return false;
    if (!writer.write(desc.header.content_hash))
      return false;
    if (!writer.write(desc.header.variant_flags))
      return false;
    if (!writer.write_blob(std::as_bytes(std::span(desc.header.reserved))))
      return false;

    // Write MaterialAssetDesc fields
    if (!writer.write(desc.material_domain))
      return false;
    if (!writer.write(desc.flags))
      return false;
    if (!writer.write(desc.shader_stages))
      return false;

    // Write float arrays element by element
    for (const auto& color_component : desc.base_color) {
      if (!writer.write(color_component))
        return false;
    }

    if (!writer.write(desc.normal_scale))
      return false;
    if (!writer.write(desc.metalness))
      return false;
    if (!writer.write(desc.roughness))
      return false;
    if (!writer.write(desc.ambient_occlusion))
      return false;

    // Write texture indices
    if (!writer.write(desc.base_color_texture))
      return false;
    if (!writer.write(desc.normal_texture))
      return false;
    if (!writer.write(desc.metallic_texture))
      return false;
    if (!writer.write(desc.roughness_texture))
      return false;
    if (!writer.write(desc.ambient_occlusion_texture))
      return false;

    // Write reserved texture array
    if (!writer.write_blob(std::as_bytes(std::span(desc.reserved_textures))))
      return false;

    // Write reserved bytes
    if (!writer.write_blob(std::as_bytes(std::span(desc.reserved))))
      return false;

    return true;
  }

  //! Helper method to create LoaderContext for testing.
  auto CreateLoaderContext() -> oxygen::content::LoaderContext<MockStream>
  {
    return oxygen::content::LoaderContext<MockStream> { .asset_loader
      = &asset_loader,
      .current_asset_key = oxygen::data::AssetKey {}, // Test asset key
      .reader = std::ref(reader),
      .offline = false };
  }

  //! Helper method to write ShaderReferenceDesc using write_blob for arrays.
  auto WriteShaderReferenceDesc(
    const oxygen::data::pak::ShaderReferenceDesc& shader_desc) -> bool
  {
    // Write shader_unique_id array as blob
    if (!writer.write_blob(
          std::as_bytes(std::span(shader_desc.shader_unique_id))))
      return false;

    if (!writer.write(shader_desc.shader_hash))
      return false;

    // Write reserved array as blob
    if (!writer.write_blob(std::as_bytes(std::span(shader_desc.reserved))))
      return false;

    return true;
  }

  MockStream stream;
  Writer writer;
  Reader<MockStream> reader;
  NiceMock<MockAssetLoader> asset_loader;
};

//=== MaterialLoader Basic Functionality Tests ===-------------------------//

//! Test: LoadMaterialAsset returns valid MaterialAsset for correct input.
NOLINT_TEST_F(
  MaterialLoaderBasicTestFixture, LoadMaterial_ValidInput_ReturnsMaterialAsset)
{
  // Arrange
  using oxygen::ShaderType;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialAsset;
  using oxygen::data::MaterialDomain;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ShaderReferenceDesc;

  constexpr uint32_t kShaderStages
    = (1 << static_cast<uint32_t>(ShaderType::kVertex))
    | (1 << static_cast<uint32_t>(ShaderType::kPixel));
  constexpr size_t kShaderCount = 2;

  MaterialAssetDesc desc{
    .header = {
      .asset_type = static_cast<uint8_t>(AssetType::kMaterial),
      .name = "Test Material",
      .version = 1,
      .streaming_priority = 2,
      .content_hash = 0x1234567890ABCDEF,
      .variant_flags = 0x870,
    },
    .material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque),
    .flags = 0xAABBCCDD,
    .shader_stages = kShaderStages,
    .base_color = {0.1f, 0.2f, 0.3f, 0.4f},
    .normal_scale = 1.5f,
    .metalness = 0.7f,
    .roughness = 0.2f,
    .ambient_occlusion = 0.9f,
    .base_color_texture = 42,
    .normal_texture = 43,
    .metallic_texture = 44,
    .roughness_texture = 45,
    .ambient_occlusion_texture = 46,
  };

  ShaderReferenceDesc shader_descs[kShaderCount] = {
    {
      .shader_unique_id = "VS@main.vert",
      .shader_hash = 0x1111,
    },
    {
      .shader_unique_id = "PS@main.frag",
      .shader_hash = 0x2222,
    },
  };

  // Write MaterialAssetDesc using field-by-field serialization
  ASSERT_TRUE(WriteMaterialAssetDesc(desc));
  // Write ShaderReferenceDesc array using field-by-field serialization
  for (size_t i = 0; i < kShaderCount; ++i) {
    ASSERT_TRUE(WriteShaderReferenceDesc(shader_descs[i]));
  }
  stream.seek(0);

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());

  // Scalars
  EXPECT_EQ(asset->GetAssetType(), AssetType::kMaterial);
  EXPECT_EQ(asset->GetAssetName(), "Test Material");
  EXPECT_EQ(asset->GetMaterialDomain(), MaterialDomain::kOpaque);
  EXPECT_EQ(asset->GetFlags(), 0xAABBCCDDu);
  EXPECT_FLOAT_EQ(asset->GetNormalScale(), 1.5f);
  EXPECT_FLOAT_EQ(asset->GetMetalness(), 0.7f);
  EXPECT_FLOAT_EQ(asset->GetRoughness(), 0.2f);
  EXPECT_FLOAT_EQ(asset->GetAmbientOcclusion(), 0.9f);

  // Arrays/collections
  EXPECT_THAT(
    asset->GetBaseColor(), ::testing::ElementsAre(0.1f, 0.2f, 0.3f, 0.4f));
  EXPECT_THAT((std::array<unsigned, 5> {
                static_cast<unsigned>(asset->GetBaseColorTexture()),
                static_cast<unsigned>(asset->GetNormalTexture()),
                static_cast<unsigned>(asset->GetMetallicTexture()),
                static_cast<unsigned>(asset->GetRoughnessTexture()),
                static_cast<unsigned>(asset->GetAmbientOcclusionTexture()) }),
    ::testing::ElementsAre(42u, 43u, 44u, 45u, 46u));

  // Shaders
  auto shaders = asset->GetShaders();
  ASSERT_THAT(shaders, ::testing::SizeIs(kShaderCount));
  EXPECT_THAT(shaders,
    ::testing::ElementsAre(
      AllOf(::testing::Property(&oxygen::data::ShaderReference::GetShaderType,
              ShaderType::kVertex),
        ::testing::Property(&oxygen::data::ShaderReference::GetShaderUniqueId,
          Eq("VS@main.vert")),
        ::testing::Property(
          &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x1111u))),
      AllOf(::testing::Property(&oxygen::data::ShaderReference::GetShaderType,
              ShaderType::kPixel),
        ::testing::Property(&oxygen::data::ShaderReference::GetShaderUniqueId,
          Eq("PS@main.frag")),
        ::testing::Property(
          &oxygen::data::ShaderReference::GetShaderSourceHash, Eq(0x2222u)))));
}

//=== MaterialLoader Error Handling Tests ===------------------------------//

//! Fixture for MaterialLoader error test cases.
class MaterialLoaderErrorTestFixture : public MaterialLoaderBasicTestFixture {
  // No additional members needed for now; extend as needed for error scenarios.
};

//! Test: LoadMaterialAsset throws if header cannot be read.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadHeader_Throws)
{
  // Arrange
  std::vector<std::byte> buffer(10, std::byte { 0 }); // Too small
  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW(
    {
      auto context = CreateLoaderContext();
      (void)LoadMaterialAsset(context);
    },
    std::runtime_error);
}

//! Test: LoadMaterialAsset throws if shader IDs are truncated.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadShaderIds_Throws)
{
  // Arrange
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52] {};
  };
  constexpr uint32_t kMaterialType = 1;
  constexpr uint32_t kShaderStages = 0b111; // 3 bits set
  constexpr uint32_t kTextureCount = 0;
  std::vector<std::byte> buffer(
    sizeof(MaterialAssetHeader) + 8 * 2); // only 2 shader IDs, should be 3
  auto* header = reinterpret_cast<MaterialAssetHeader*>(buffer.data());
  header->material_type = kMaterialType;
  header->shader_stages = kShaderStages;
  header->texture_count = kTextureCount;
  uint64_t shader_ids[2] = { 123, 456 };
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader), shader_ids, 2 * 8);

  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW(
    {
      auto context = CreateLoaderContext();
      (void)LoadMaterialAsset(context);
    },
    std::runtime_error);
}

//! Test: LoadMaterialAsset throws if texture IDs are truncated.
NOLINT_TEST_F(
  MaterialLoaderErrorTestFixture, LoadMaterial_FailsToReadTextureIds_Throws)
{
  // Arrange
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52] {};
  };
  constexpr uint32_t kMaterialType = 1;
  constexpr uint32_t kShaderStages = 0b1; // 1 shader ID
  constexpr uint32_t kTextureCount = 2;
  std::vector<std::byte> buffer(sizeof(MaterialAssetHeader) + 8 * 1
    + 8 * 1); // only 1 texture ID, should be 2
  auto* header = reinterpret_cast<MaterialAssetHeader*>(buffer.data());
  header->material_type = kMaterialType;
  header->shader_stages = kShaderStages;
  header->texture_count = kTextureCount;
  uint64_t shader_id = 123;
  uint64_t texture_id = 456;
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader), &shader_id, 8);
  std::memcpy(buffer.data() + sizeof(MaterialAssetHeader) + 8, &texture_id, 8);

  stream.write(buffer.data(), buffer.size());
  stream.seek(0);

  // Act + Assert
  EXPECT_THROW(
    {
      auto context = CreateLoaderContext();
      (void)LoadMaterialAsset(context);
    },
    std::runtime_error);
}

//=== MaterialLoader Dependency Management Tests ===------------------------//

//! Test: LoadMaterialAsset registers resource dependencies for textures.
NOLINT_TEST_F(MaterialLoaderBasicTestFixture,
  LoadMaterial_ValidTextures_RegistersResourceDependencies)
{
  // Arrange
  using oxygen::ShaderType;
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ShaderReferenceDesc;

  constexpr uint32_t kShaderStages
    = (1 << static_cast<uint32_t>(ShaderType::kVertex));

  // Set up material with texture dependencies
  MaterialAssetDesc desc{
    .header = {
      .asset_type = static_cast<uint8_t>(AssetType::kMaterial),
      .name = "Test Material",
      .version = 1,
      .streaming_priority = 2,
      .content_hash = 0x1234567890ABCDEF,
      .variant_flags = 0x870,
    },
    .material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque),
    .flags = 0xAABBCCDD,
    .shader_stages = kShaderStages,
    .base_color = {0.1f, 0.2f, 0.3f, 0.4f},
    .normal_scale = 1.5f,
    .metalness = 0.7f,
    .roughness = 0.2f,
    .ambient_occlusion = 0.9f,
    .base_color_texture = 100,      // Non-zero texture indices
    .normal_texture = 101,
    .metallic_texture = 102,
    .roughness_texture = 103,
    .ambient_occlusion_texture = 104,
  };

  ShaderReferenceDesc shader_desc {
    .shader_unique_id = "VS@main.vert",
    .shader_hash = 0x1111,
  };

  // Write test data
  ASSERT_TRUE(WriteMaterialAssetDesc(desc));
  ASSERT_TRUE(WriteShaderReferenceDesc(shader_desc));
  stream.seek(0);

  // Expect resource dependency registrations for each texture
  EXPECT_CALL(asset_loader,
    AddResourceDependency(::testing::_, 100)); // base_color_texture
  EXPECT_CALL(
    asset_loader, AddResourceDependency(::testing::_, 101)); // normal_texture
  EXPECT_CALL(
    asset_loader, AddResourceDependency(::testing::_, 102)); // metallic_texture
  EXPECT_CALL(asset_loader,
    AddResourceDependency(::testing::_, 103)); // roughness_texture
  EXPECT_CALL(asset_loader,
    AddResourceDependency(::testing::_, 104)); // ambient_occlusion_texture

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
}

//! Test: LoadMaterialAsset registers asset dependencies for shaders.
NOLINT_TEST_F(MaterialLoaderBasicTestFixture,
  LoadMaterial_ValidShaders_RegistersAssetDependencies)
{
  // Arrange
  using oxygen::ShaderType;
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ShaderReferenceDesc;

  constexpr uint32_t kShaderStages
    = (1 << static_cast<uint32_t>(ShaderType::kVertex))
    | (1 << static_cast<uint32_t>(ShaderType::kPixel));
  constexpr size_t kShaderCount = 2;

  MaterialAssetDesc desc{
    .header = {
      .asset_type = static_cast<uint8_t>(AssetType::kMaterial),
      .name = "Test Material",
      .version = 1,
      .streaming_priority = 2,
      .content_hash = 0x1234567890ABCDEF,
      .variant_flags = 0x870,
    },
    .material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque),
    .flags = 0xAABBCCDD,
    .shader_stages = kShaderStages,
    .base_color = {0.1f, 0.2f, 0.3f, 0.4f},
    .normal_scale = 1.5f,
    .metalness = 0.7f,
    .roughness = 0.2f,
    .ambient_occlusion = 0.9f,
    .base_color_texture = 0,      // Zero texture indices (no texture dependencies)
    .normal_texture = 0,
    .metallic_texture = 0,
    .roughness_texture = 0,
    .ambient_occlusion_texture = 0,
  };

  ShaderReferenceDesc shader_descs[kShaderCount] = {
    {
      .shader_unique_id = "VS@main.vert",
      .shader_hash = 0x1111,
    },
    {
      .shader_unique_id = "PS@main.frag",
      .shader_hash = 0x2222,
    },
  };

  // Write test data
  ASSERT_TRUE(WriteMaterialAssetDesc(desc));
  for (size_t i = 0; i < kShaderCount; ++i) {
    ASSERT_TRUE(WriteShaderReferenceDesc(shader_descs[i]));
  }
  stream.seek(0);

  // Expect asset dependency registrations for each shader
  // FIXME: These calls will fail until dependency registration is implemented
  // in LoadMaterialAsset
  EXPECT_CALL(asset_loader, AddAssetDependency(::testing::_, ::testing::_))
    .Times(kShaderCount);

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
}

//! Test: LoadMaterialAsset skips zero texture indices (no dependencies).
NOLINT_TEST_F(
  MaterialLoaderBasicTestFixture, LoadMaterial_ZeroTextures_SkipsDependencies)
{
  // Arrange
  using oxygen::ShaderType;
  using oxygen::data::AssetType;
  using oxygen::data::MaterialDomain;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ShaderReferenceDesc;

  constexpr uint32_t kShaderStages
    = (1 << static_cast<uint32_t>(ShaderType::kVertex));

  MaterialAssetDesc desc{
    .header = {
      .asset_type = static_cast<uint8_t>(AssetType::kMaterial),
      .name = "Test Material",
      .version = 1,
      .streaming_priority = 2,
      .content_hash = 0x1234567890ABCDEF,
      .variant_flags = 0x870,
    },
    .material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque),
    .flags = 0xAABBCCDD,
    .shader_stages = kShaderStages,
    .base_color = {0.1f, 0.2f, 0.3f, 0.4f},
    .normal_scale = 1.5f,
    .metalness = 0.7f,
    .roughness = 0.2f,
    .ambient_occlusion = 0.9f,
    .base_color_texture = 0,      // All zero - no texture dependencies expected
    .normal_texture = 0,
    .metallic_texture = 0,
    .roughness_texture = 0,
    .ambient_occlusion_texture = 0,
  };

  ShaderReferenceDesc shader_desc {
    .shader_unique_id = "VS@main.vert",
    .shader_hash = 0x1111,
  };

  // Write test data
  ASSERT_TRUE(WriteMaterialAssetDesc(desc));
  ASSERT_TRUE(WriteShaderReferenceDesc(shader_desc));
  stream.seek(0);

  // Expect NO resource dependency calls for zero texture indices
  EXPECT_CALL(asset_loader, AddResourceDependency(::testing::_, ::testing::_))
    .Times(0);

  // Act
  auto context = CreateLoaderContext();
  auto asset = LoadMaterialAsset(context);

  // Assert
  ASSERT_THAT(asset, NotNull());
}

} // namespace
