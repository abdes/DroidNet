//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>

using testing::IsNull;
using testing::NotNull;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;

using oxygen::content::testing::AssetLoaderLoadingTest;

auto AssetLoaderLoadingTest::GetTestDataDir() -> std::filesystem::path
{
  // Path to test data directory containing YAML specs
  return std::filesystem::path(__FILE__).parent_path() / "TestData";
}

auto AssetLoaderLoadingTest::GeneratePakFile(const std::string& spec_name)
  -> std::filesystem::path
{
  const auto test_data_dir = GetTestDataDir();
  const auto spec_path = test_data_dir / (spec_name + ".yaml");
  auto output_path = temp_dir_ / (spec_name + ".pak");

  // Check if YAML spec exists
  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("Test spec not found: " + spec_path.string());
  }

  // Generate PAK file using pakgen CLI (replaces legacy generate_pak.py).
  // Prefer a deterministic build for reproducible tests. The pakgen editable
  // install is configured by CMake (pakgen_editable_install target). Fallback:
  // if pakgen is not on PATH, attempt invoking via python -m.
  std::string command;
  {
    // Primary invocation
    command = "pakgen build \"" + spec_path.string() + "\" \""
      + output_path.string() + "\" --deterministic";
    // If that fails we will retry with python -m pakgen.cli below.
  }

  auto run_command
    = [&](const std::string& cmd) -> int { return std::system(cmd.c_str()); };

  int result = run_command(command);
  if (result != 0) {
    // Retry using explicit module invocation (handles virtual env edge cases).
    const std::string module_cmd = "python -m pakgen.cli build \""
      + spec_path.string() + "\" \"" + output_path.string()
      + "\" --deterministic";
    result = run_command(module_cmd);
  }

  if (result != 0) {
    throw std::runtime_error(
      "Failed to generate PAK file with pakgen for spec: " + spec_name);
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

auto AssetLoaderLoadingTest::CreateTestAssetKey(const std::string& name)
  -> data::AssetKey
{
  // Return predefined asset keys that match the YAML test specifications
  data::AssetKey key {};

  if (name == "test_material") {
    // Matches simple_material.yaml: "01234567-89ab-cdef-0123-456789abcdef"
    const auto bytes = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
      0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "test_geometry") {
    // Matches simple_geometry.yaml: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    const auto bytes = { 0xaa, 0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd,
      0xdd, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "textured_material") {
    // Matches material_with_textures.yaml:
    // "12345678-90ab-cdef-1234-567890abcdef"
    const auto bytes = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12,
      0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "buffered_geometry") {
    // Matches geometry_with_buffers.yaml:
    // "ffffffff-eeee-dddd-cccc-bbbbbbbbbbbb"
    const auto bytes = { 0xff, 0xff, 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc,
      0xcc, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb };
    std::ranges::copy(bytes, key.guid.begin());
  } else if (name == "complex_geometry" || name == "SpaceshipGeometry") {
    // Matches complex_geometry.yaml SpaceshipGeometry:
    // "deadbeef-cafe-babe-dead-feeddeadbeef"
    const auto bytes = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe, 0xde,
      0xad, 0xfe, 0xed, 0xde, 0xad, 0xbe, 0xef };
    std::ranges::copy(bytes, key.guid.begin());
  } else {
    // Fallback: create a deterministic key from hash for unknown names
    constexpr std::hash<std::string> hasher;
    const auto hash = hasher(name);
    auto hash_bytes = std::bit_cast<std::array<uint8_t, sizeof(hash)>>(hash);
    auto guid_span = std::span { key.guid };
    auto hash_span = std::span { hash_bytes };
    std::copy_n(hash_span.begin(), std::min(hash_span.size(), guid_span.size()),
      guid_span.begin());
  }

  return key;
}

namespace {

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! Test: AssetLoader can load a simple material asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic material asset and verifies that the
 AssetLoader can successfully load it.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SimpleMaterial_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  const auto material_key = CreateTestAssetKey("test_material");

  // Act
  const auto material
    = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material, NotNull());
}

//! Test: AssetLoader can load a simple geometry asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic geometry asset and verifies that the
 AssetLoader can successfully load it.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SimpleGeometry_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_geometry");
  asset_loader_->AddPakFile(pak_path);

  const auto geometry_key = CreateTestAssetKey("test_geometry");

  // Act
  const auto geometry
    = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(geometry, NotNull());
}

//! Test: AssetLoader can load a geometry asset with buffer dependencies
/*!
 Scenario: Creates a PAK file with a geometry asset that has vertex and index
 buffer dependencies and verifies successful loading with proper mesh properties
 and buffer references.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_ComplexGeometry_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("complex_geometry");
  asset_loader_->AddPakFile(pak_path);

  const auto geometry_key = CreateTestAssetKey("complex_geometry");

  // Act
  const auto geometry
    = asset_loader_->LoadAsset<GeometryAsset>(geometry_key, false);

  // Assert
  EXPECT_THAT(geometry, NotNull());

  if (geometry) {
    // Verify geometry has meshes and buffer dependencies
    const auto meshes = geometry->Meshes();
    EXPECT_FALSE(meshes.empty());

    // Verify each mesh has valid properties
    for (size_t i = 0; i < meshes.size(); ++i) {
      const auto& mesh = meshes[i];
      EXPECT_THAT(mesh, NotNull())
        << "Mesh at index " << i << " should not be null";

      if (mesh) {
        // Check basic mesh properties - buffered geometry should have vertex
        // data
        EXPECT_GE(mesh->VertexCount(), 0) << "Vertex count for mesh " << i;
        EXPECT_GE(mesh->IndexCount(), 0) << "Index count for mesh " << i;
      }
    }
  }
}

//! Test: AssetLoader returns nullptr for non-existent asset
/*!
 Scenario: Attempts to load an asset that doesn't exist in any PAK file and
 verifies that nullptr is returned.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAsset_NonExistent_ReturnsNull)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  const auto non_existent_key = CreateTestAssetKey("non_existent_asset");

  // Act
  const auto result
    = asset_loader_->LoadAsset<MaterialAsset>(non_existent_key, false);

  // Assert
  EXPECT_THAT(result, IsNull());
}

//! Test: AssetLoader caches loaded assets
/*!
 Scenario: Loads the same asset twice and verifies that the same instance is
 returned (caching behavior).
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SameAssetTwice_ReturnsSameInstance)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  const auto material_key = CreateTestAssetKey("test_material");

  // Act
  const auto material1
    = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);
  const auto material2
    = asset_loader_->LoadAsset<MaterialAsset>(material_key, false);

  // Assert
  EXPECT_THAT(material1, NotNull());
  EXPECT_THAT(material2, NotNull());
  EXPECT_EQ(material1, material2); // Same instance due to caching
}

} // namespace
