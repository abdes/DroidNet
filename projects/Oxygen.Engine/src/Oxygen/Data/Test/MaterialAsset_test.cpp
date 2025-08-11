//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <memory>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/MaterialAsset.h>

using oxygen::data::MaterialAsset;

//=== MaterialAsset Tests ===------------------------------------------------//

namespace {

using ::testing::NotNull;
using ::testing::SizeIs;

//! Tests MaterialAsset default material creation.
NOLINT_TEST(MaterialAssetBasicTest, CreateDefault_ReturnsValidMaterial)
{
  // Arrange
  // (No setup needed beyond calling the factory.)

  // Act
  auto default_material = MaterialAsset::CreateDefault();

  // Assert
  EXPECT_THAT(default_material, NotNull());
  EXPECT_EQ(default_material->GetMaterialType(), 1u); // Basic/Unlit
  EXPECT_EQ(default_material->GetShaderStages(), 0x09u); // Vertex + Fragment
  EXPECT_EQ(default_material->GetTextureCount(), 0u); // No textures
  EXPECT_THAT(default_material->GetShaderIds(),
    SizeIs(2)); // Vertex + Fragment shader IDs
  EXPECT_THAT(default_material->GetTextureIds(), SizeIs(0)); // No texture IDs
}

//! Tests MaterialAsset default material is reusable.
NOLINT_TEST(MaterialAssetBasicTest, CreateDefault_ReturnsDistinctInstances)
{
  // Arrange
  // (No pre-existing state required.)

  // Act
  auto material1 = MaterialAsset::CreateDefault();
  auto material2 = MaterialAsset::CreateDefault();

  // Assert
  EXPECT_THAT(material1, NotNull());
  EXPECT_THAT(material2, NotNull());
  // Each call creates a new instance (not cached)
  EXPECT_NE(material1.get(), material2.get());
  // But they have the same properties
  EXPECT_EQ(material1->GetMaterialType(), material2->GetMaterialType());
  EXPECT_EQ(material1->GetShaderStages(), material2->GetShaderStages());
}

//! Tests MaterialAsset debug material creation.
NOLINT_TEST(MaterialAssetBasicTest, CreateDebug_ReturnsValidMaterial)
{
  // Arrange
  // (No setup beyond factory invocation.)

  // Act
  auto debug_material = MaterialAsset::CreateDebug();

  // Assert
  EXPECT_THAT(debug_material, NotNull());
  // Debug material should share same basic material type as default
  EXPECT_EQ(debug_material->GetMaterialType(), 1u);
  // Should target same core shader stages (vertex + fragment) for visibility
  EXPECT_EQ(debug_material->GetShaderStages(), 0x09u);
  // No bound textures by default
  EXPECT_EQ(debug_material->GetTextureCount(), 0u);
  EXPECT_THAT(debug_material->GetShaderIds(), SizeIs(2));
  EXPECT_THAT(debug_material->GetTextureIds(), SizeIs(0));
}

//! Tests default material domain and flags (ID 30).
NOLINT_TEST(MaterialAssetBasicTest, DefaultMaterialDomainAndFlags)
{
  // Arrange & Act
  auto mat = MaterialAsset::CreateDefault();

  // Assert
  ASSERT_THAT(mat, NotNull());
  EXPECT_EQ(mat->GetMaterialDomain(), oxygen::data::MaterialDomain::kOpaque);
  EXPECT_EQ(mat->GetFlags(), 0u);
}

//! Tests default material texture indices unset (ID 31).
NOLINT_TEST(MaterialAssetBasicTest, DefaultTextureIndicesUnset)
{
  // Arrange & Act
  auto mat = MaterialAsset::CreateDefault();

  // Assert
  ASSERT_THAT(mat, NotNull());
  EXPECT_EQ(mat->GetBaseColorTexture(), 0u);
  EXPECT_EQ(mat->GetNormalTexture(), 0u);
  EXPECT_EQ(mat->GetMetallicTexture(), 0u);
  EXPECT_EQ(mat->GetRoughnessTexture(), 0u);
  EXPECT_EQ(mat->GetAmbientOcclusionTexture(), 0u);
}

//! Tests default scalar/base color stability (ID 33).
NOLINT_TEST(MaterialAssetBasicTest, DefaultScalarsStable)
{
  // Arrange & Act
  auto mat = MaterialAsset::CreateDefault();

  // Assert
  ASSERT_THAT(mat, NotNull());
  auto base_color = mat->GetBaseColor();
  EXPECT_FLOAT_EQ(base_color[0], 1.0f);
  EXPECT_FLOAT_EQ(base_color[1], 1.0f);
  EXPECT_FLOAT_EQ(base_color[2], 1.0f);
  EXPECT_FLOAT_EQ(base_color[3], 1.0f);
  EXPECT_FLOAT_EQ(mat->GetNormalScale(), 1.0f);
  EXPECT_FLOAT_EQ(mat->GetMetalness(), 0.0f);
  EXPECT_FLOAT_EQ(mat->GetRoughness(), 0.8f);
  EXPECT_FLOAT_EQ(mat->GetAmbientOcclusion(), 1.0f);
}

//! Tests shader reference array size matches popcount of stage mask (ID 32).
NOLINT_TEST(MaterialAssetConsistencyTest, ShaderRefsMatchStageMask)
{
  using oxygen::ShaderType;
  using oxygen::data::ShaderReference;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::ShaderReferenceDesc;

  // Arrange
  MaterialAssetDesc desc {};
  // Set bits for vertex (bit index 2 => 1<<2 = 0x04) and pixel (bit index 6 =>
  // 1<<6 = 0x40)
  desc.shader_stages = (1u << static_cast<uint32_t>(ShaderType::kVertex))
    | (1u << static_cast<uint32_t>(ShaderType::kPixel));

  ShaderReferenceDesc vs_desc {};
  std::memcpy(vs_desc.shader_unique_id, "VS@Path/Vert.hlsl", 18);
  vs_desc.shader_hash = 0x1234ULL;
  ShaderReferenceDesc ps_desc {};
  std::memcpy(ps_desc.shader_unique_id, "PS@Path/Frag.hlsl", 18);
  ps_desc.shader_hash = 0x5678ULL;

  std::vector<ShaderReference> refs;
  refs.emplace_back(ShaderType::kVertex, vs_desc);
  refs.emplace_back(ShaderType::kPixel, ps_desc);

  // Act
  MaterialAsset material { desc, refs };
  auto shaders = material.GetShaders();

  // Assert
  auto popcount = [](uint32_t v) {
    unsigned c = 0;
    while (v) {
      v &= (v - 1);
      ++c;
    }
    return c;
  };
  ASSERT_EQ(shaders.size(), popcount(desc.shader_stages));
}

//! Tests ShaderReference construction & accessors (ID 34).
NOLINT_TEST(ShaderReferenceBasicTest, ConstructionAndAccessors)
{
  using oxygen::ShaderType;
  using oxygen::data::ShaderReference;
  using oxygen::data::pak::ShaderReferenceDesc;

  // Arrange
  ShaderReferenceDesc desc {};
  constexpr const char* kId = "VS@shaders/Basic.vert";
  std::memcpy(desc.shader_unique_id, kId, std::strlen(kId));
  desc.shader_hash = 0xCAFEBABECAFELL; // Arbitrary hash

  // Act
  ShaderReference ref { ShaderType::kVertex, desc };

  // Assert
  EXPECT_EQ(ref.GetShaderType(), ShaderType::kVertex);
  EXPECT_EQ(ref.GetShaderUniqueId(), kId);
  EXPECT_EQ(ref.GetShaderSourceHash(), 0xCAFEBABECAFELL);
}

} // namespace
