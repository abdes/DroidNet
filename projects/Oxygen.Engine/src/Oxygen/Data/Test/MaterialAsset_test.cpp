//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/MaterialAsset.h>

using oxygen::data::MaterialAsset;

//=== MaterialAsset Tests ===------------------------------------------------//

namespace {

using ::testing::NotNull;
using ::testing::SizeIs;

//! Tests MaterialAsset default material creation.
NOLINT_TEST(MaterialAssetBasicTest, CreateDefault_ReturnsValidMaterial)
{
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

} // namespace
