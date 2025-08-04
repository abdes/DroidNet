//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

//! Base test fixture for AssetLoader tests using real PAK files
/*!
 Uses the generate_pak.py tool to create test PAK files from YAML specs.
 This provides realistic testing without complex mocking infrastructure.
*/
class AssetLoaderTestBase : public ::testing::Test {
protected:
  auto SetUp() -> void override;
  auto TearDown() -> void override;

  //! Get path to test data directory
  auto GetTestDataDir() const -> std::filesystem::path;

  //! Generate a PAK file from YAML spec using generate_pak.py
  auto GeneratePakFile(const std::string& spec_name) -> std::filesystem::path;

  //! Create a simple test asset key
  auto CreateTestAssetKey(const std::string& name) const -> data::AssetKey;

  std::unique_ptr<AssetLoader> asset_loader_;
  std::filesystem::path temp_dir_;
  std::vector<std::filesystem::path> generated_paks_;
};

//! Fixture for basic AssetLoader functionality tests
class AssetLoaderBasicTest : public AssetLoaderTestBase { };

//! Fixture for AssetLoader error handling tests
class AssetLoaderErrorTest : public AssetLoaderTestBase { };

//! Fixture for AssetLoader dependency tests
class AssetLoaderDependencyTest : public AssetLoaderTestBase { };

} // namespace oxygen::content::testing
