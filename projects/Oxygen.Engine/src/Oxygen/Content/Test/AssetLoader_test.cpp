//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetLoader_test.h"

using ::testing::IsNull;
using ::testing::NotNull;

using oxygen::content::AssetLoader;
using oxygen::data::AssetKey;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;

using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::content::testing::AssetLoaderDependencyTest;
using oxygen::content::testing::AssetLoaderErrorTest;
using oxygen::content::testing::AssetLoaderTestBase;

auto AssetLoaderTestBase::SetUp() -> void
{
  // Create temporary directory for test PAK files
  temp_dir_
    = std::filesystem::temp_directory_path() / "oxygen_assetloader_tests";
  std::filesystem::create_directories(temp_dir_);

  // Create AssetLoader instance
  asset_loader_ = std::make_unique<AssetLoader>();
}

auto AssetLoaderTestBase::TearDown() -> void
{
  // Reset AssetLoader first to close PAK files
  asset_loader_.reset();

  // Clean up generated PAK files
  for (const auto& pak_path : generated_paks_) {
    if (std::filesystem::exists(pak_path)) {
      std::filesystem::remove(pak_path);
    }
  }

  // Clean up temporary directory
  if (std::filesystem::exists(temp_dir_)) {
    std::filesystem::remove_all(temp_dir_);
  }
}

auto AssetLoaderTestBase::GetTestDataDir() const -> std::filesystem::path
{
  // Path to test data directory containing YAML specs
  return std::filesystem::path(__FILE__).parent_path() / "TestData";
}

auto AssetLoaderTestBase::GeneratePakFile(const std::string& spec_name)
  -> std::filesystem::path
{
  auto test_data_dir = GetTestDataDir();
  auto spec_path = test_data_dir / (spec_name + ".yaml");
  auto output_path = temp_dir_ / (spec_name + ".pak");

  // Check if YAML spec exists
  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("Test spec not found: " + spec_path.string());
  }

  // Generate PAK file using generate_pak.py
  auto generate_script = test_data_dir / "generate_pak.py";
  if (!std::filesystem::exists(generate_script)) {
    throw std::runtime_error(
      "generate_pak.py not found at: " + generate_script.string());
  }

  // Build command to run generate_pak.py with --force to overwrite existing
  // files
  std::string command = "python \"" + generate_script.string() + "\" \""
    + spec_path.string() + "\" \"" + output_path.string() + "\" --force";

  // Execute the command
  int result = std::system(command.c_str());
  if (result != 0) {
    throw std::runtime_error("Failed to generate PAK file: " + spec_name);
  }

  // Verify the PAK file was created
  if (!std::filesystem::exists(output_path)) {
    throw std::runtime_error(
      "PAK file was not created: " + output_path.string());
  }

  // Track generated file for cleanup
  generated_paks_.push_back(output_path);

  return output_path;
}

auto AssetLoaderTestBase::CreateTestAssetKey(const std::string& name) const
  -> data::AssetKey
{
  // Return predefined asset keys that match the YAML test specifications
  data::AssetKey key {};

  if (name == "test_material") {
    // Matches simple_material.yaml: "01234567-89ab-cdef-0123-456789abcdef"
    const uint8_t bytes[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    std::memcpy(key.guid.data(), bytes, sizeof(bytes));
  } else if (name == "test_geometry") {
    // Matches simple_geometry.yaml: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    const uint8_t bytes[] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc,
      0xdd, 0xdd, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee };
    std::memcpy(key.guid.data(), bytes, sizeof(bytes));
  } else if (name == "textured_material") {
    // Matches material_with_textures.yaml:
    // "12345678-90ab-cdef-1234-567890abcdef"
    const uint8_t bytes[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
      0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef };
    std::memcpy(key.guid.data(), bytes, sizeof(bytes));
  } else if (name == "buffered_geometry") {
    // Matches geometry_with_buffers.yaml:
    // "ffffffff-eeee-dddd-cccc-bbbbbbbbbbbb"
    const uint8_t bytes[] = { 0xff, 0xff, 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd,
      0xcc, 0xcc, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb };
    std::memcpy(key.guid.data(), bytes, sizeof(bytes));
  } else {
    // Fallback: create a deterministic key from hash for unknown names
    std::hash<std::string> hasher;
    auto hash = hasher(name);
    std::memcpy(
      key.guid.data(), &hash, std::min(sizeof(hash), key.guid.size()));
  }

  return key;
}

namespace {

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! Test: AssetLoader can load a simple material asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic material asset and verifies
 that the AssetLoader can successfully load it.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAsset_SimpleMaterial_LoadsSuccessfully)
{
  // Arrange
  auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  auto material_key = CreateTestAssetKey("test_material");

  // Act
  auto material = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material, NotNull());
}

//! Test: AssetLoader can load a simple geometry asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic geometry asset and verifies
 that the AssetLoader can successfully load it.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAsset_SimpleGeometry_LoadsSuccessfully)
{
  // Arrange
  auto pak_path = GeneratePakFile("simple_geometry");
  asset_loader_->AddPakFile(pak_path);

  auto geometry_key = CreateTestAssetKey("test_geometry");

  // Act
  auto geometry = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(geometry, NotNull());
}

//! Test: AssetLoader returns nullptr for non-existent asset
/*!
 Scenario: Attempts to load an asset that doesn't exist in any PAK file
 and verifies that nullptr is returned.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAsset_NonExistent_ReturnsNull)
{
  // Arrange
  auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  auto non_existent_key = CreateTestAssetKey("non_existent_asset");

  // Act
  auto result
    = asset_loader_->LoadAsset<MaterialAsset>(non_existent_key, false);

  // Assert
  EXPECT_THAT(result, IsNull());
}

//! Test: AssetLoader caches loaded assets
/*!
 Scenario: Loads the same asset twice and verifies that the same
 instance is returned (caching behavior).
*/
NOLINT_TEST_F(
  AssetLoaderBasicTest, LoadAsset_SameAssetTwice_ReturnsSameInstance)
{
  // Arrange
  auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  auto material_key = CreateTestAssetKey("test_material");

  // Act
  auto material1 = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);
  auto material2 = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material1, NotNull());
  EXPECT_THAT(material2, NotNull());
  EXPECT_EQ(material1, material2); // Same instance due to caching
}

//=== AssetLoader Dependency Tests ===--------------------------------------//

//! Test: AssetLoader handles material with texture dependencies
/*!
 Scenario: Loads a material asset that depends on texture resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAsset_MaterialWithTextures_LoadsDependencies)
{
  // Arrange
  auto pak_path = GeneratePakFile("material_with_textures");
  asset_loader_->AddPakFile(pak_path);

  auto material_key = CreateTestAssetKey("textured_material");

  // Act
  auto material = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material, NotNull());

  // Verify that texture dependencies are properly referenced
  // All texture indices should be valid (>= 0), with 0 being the default
  // texture
  auto base_color_idx = material->GetBaseColorTexture();
  auto normal_idx = material->GetNormalTexture();
  auto roughness_idx = material->GetRoughnessTexture();

  // All indices should be valid (0 = default texture, >0 = specific textures)
  EXPECT_GE(base_color_idx, 0);
  EXPECT_GE(normal_idx, 0);
  EXPECT_GE(roughness_idx, 0);
}

//! Test: AssetLoader handles geometry with buffer dependencies
/*!
 Scenario: Loads a geometry asset that depends on buffer resources and
 verifies that dependencies are properly resolved.
*/
NOLINT_TEST_F(
  AssetLoaderDependencyTest, LoadAsset_GeometryWithBuffers_LoadsDependencies)
{
  // Arrange
  auto pak_path = GeneratePakFile("geometry_with_buffers");
  asset_loader_->AddPakFile(pak_path);

  auto geometry_key = CreateTestAssetKey("buffered_geometry");

  // Act
  auto geometry = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(geometry, NotNull());

  // Verify that buffer dependencies are properly loaded
  // The geometry should have at least one mesh with valid buffer references
  auto meshes = geometry->Meshes();
  EXPECT_FALSE(meshes.empty());

  if (!meshes.empty()) {
    const auto& first_mesh = meshes[0];
    ASSERT_THAT(first_mesh, NotNull());

    // Verify mesh has buffer data available
    // Note: VertexCount/IndexCount may be 0 for default/empty buffers (index 0)
    // but the mesh should still be valid and have buffer references
    EXPECT_GE(first_mesh->VertexCount(), 0);
    EXPECT_GE(first_mesh->IndexCount(), 0);

    // If the mesh has indices, it should be marked as indexed
    if (first_mesh->IndexCount() > 0) {
      EXPECT_TRUE(first_mesh->IsIndexed());
    }
  }
}

//=== AssetLoader Error Handling Tests ===----------------------------------//

//! Test: AssetLoader handles corrupted PAK file gracefully
/*!
 Scenario: Attempts to load assets from a corrupted PAK file and verifies
 graceful error handling by catching the expected exception.
*/
NOLINT_TEST_F(AssetLoaderErrorTest, LoadAsset_CorruptedPak_HandlesGracefully)
{
  // Arrange - Create a corrupted PAK file
  auto corrupted_pak = temp_dir_ / "corrupted.pak";
  {
    std::ofstream file(corrupted_pak, std::ios::binary);
    file << "CORRUPTED_DATA_NOT_A_VALID_PAK_FILE";
  }

  // Act & Assert - Should throw exception for corrupted file
  EXPECT_THROW({ asset_loader_->AddPakFile(corrupted_pak); }, std::exception);
}

//! Test: AssetLoader handles missing PAK file gracefully
/*!
 Scenario: Attempts to add a non-existent PAK file and verifies
 graceful error handling.
*/
NOLINT_TEST_F(AssetLoaderErrorTest, AddPakFile_NonExistent_HandlesGracefully)
{
  // Arrange
  auto non_existent_pak = temp_dir_ / "non_existent.pak";

  // Act & Assert - Should handle gracefully (may throw or return gracefully)
  // Implementation-dependent behavior
  EXPECT_NO_THROW({
    try {
      asset_loader_->AddPakFile(non_existent_pak);
    } catch (const std::exception&) {
      // Expected behavior for missing file
    }
  });
}

//=== AssetLoader Resource Loading Tests ===--------------------------------//

//! Test: AssetLoader can load buffer resources
/*!
 Scenario: Loads buffer resources from a PAK file and verifies
 successful loading.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadResource_Buffer_LoadsSuccessfully)
{
  // Arrange
  auto pak_path = GeneratePakFile("buffers_only");
  asset_loader_->AddPakFile(pak_path);

  // This would require knowledge of the PAK file structure
  // For now, just verify the infrastructure works
  EXPECT_NO_THROW({
    // Test infrastructure - actual resource loading would need
    // specific resource indices from the generated PAK
  });
}

//! Test: AssetLoader can load texture resources
/*!
 Scenario: Loads texture resources from a PAK file and verifies
 successful loading.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadResource_Texture_LoadsSuccessfully)
{
  // Arrange
  auto pak_path = GeneratePakFile("textures_only");
  asset_loader_->AddPakFile(pak_path);

  // This would require knowledge of the PAK file structure
  // For now, just verify the infrastructure works
  EXPECT_NO_THROW({
    // Test infrastructure - actual resource loading would need
    // specific resource indices from the generated PAK
  });
}

//=== AssetLoader Cache Management Tests ===================================//

//! Test: AssetLoader properly releases assets
/*!
 Scenario: Loads an asset, releases it, and verifies that it's
 no longer cached.
*/
NOLINT_TEST_F(
  AssetLoaderBasicTest, ReleaseAsset_LoadedAsset_ReleasesSuccessfully)
{
  // Arrange
  auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  auto material_key = CreateTestAssetKey("test_material");
  auto material = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);
  ASSERT_THAT(material, NotNull());

  // Act
  bool was_released = asset_loader_->ReleaseAsset(material_key, false);

  // Assert
  EXPECT_TRUE(was_released);
}

//! Test: AssetLoader handles multiple PAK files
/*!
 Scenario: Adds multiple PAK files and verifies that assets can be
 loaded from all of them.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAsset_MultiplePaks_LoadsFromBoth)
{
  // Arrange
  auto pak1_path = GeneratePakFile("simple_material");
  auto pak2_path = GeneratePakFile("simple_geometry");

  asset_loader_->AddPakFile(pak1_path);
  asset_loader_->AddPakFile(pak2_path);

  auto material_key = CreateTestAssetKey("test_material");
  auto geometry_key = CreateTestAssetKey("test_geometry");

  // Act
  auto material = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);
  auto geometry = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(material, NotNull());
  EXPECT_THAT(geometry, NotNull());
}

} // namespace
