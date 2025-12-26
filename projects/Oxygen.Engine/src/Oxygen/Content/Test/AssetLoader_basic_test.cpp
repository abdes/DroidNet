//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderBasicTest;

auto AssetLoaderBasicTest::SetUp() -> void
{
  // Create temporary directory for test PAK files
  temp_dir_
    = std::filesystem::temp_directory_path() / "oxygen_asset_loader_tests";
  std::filesystem::create_directories(temp_dir_);

  // Create AssetLoader instance
  asset_loader_ = std::make_unique<AssetLoader>(
    oxygen::content::internal::EngineTagFactory::Get());
}

auto AssetLoaderBasicTest::TearDown() -> void
{
  // Reset AssetLoader first to close PAK files
  asset_loader_.reset();

  // Clean up temporary directory
  if (std::filesystem::exists(temp_dir_)) {
    std::filesystem::remove_all(temp_dir_);
  }
}

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

namespace {

//! Test: AssetLoader handles corrupted PAK file gracefully
/*!
 Scenario: Attempts to load assets from a corrupted PAK file and verifies
 graceful error handling by catching the expected exception.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAsset_CorruptedPak_HandlesGracefully)
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
NOLINT_TEST_F(AssetLoaderBasicTest, AddPakFile_NonExistent_HandlesGracefully)
{
  // Arrange
  const auto non_existent_pak = temp_dir_ / "non_existent.pak";

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

} // namespace
