//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/EngineTag.h>

namespace oxygen::content::testing {

//! Base test fixture for AssetLoader tests using real PAK files
class AssetLoaderBasicTest : public ::testing::Test {
protected:
  using Tag = oxygen::engine::internal::EngineTagFactory;

  auto SetUp() -> void override;
  auto TearDown() -> void override;

  std::filesystem::path temp_dir_;
  std::unique_ptr<AssetLoader> asset_loader_;
};

//! Advanced loading test cases fixture, using real PAK files.
/*!
 Creates test PAK files from YAML specs through the shared content-test
 * helper.
 This provides realistic coverage without complex mocking
 * infrastructure.
*/
class AssetLoaderLoadingTest : public AssetLoaderBasicTest {
protected:
  //! Get path to test data directory
  [[nodiscard]] static auto GetTestDataDir() -> std::filesystem::path;

  //! Generate a PAK file from a YAML spec.
  auto GeneratePakFile(const std::string& spec_name) -> std::filesystem::path;

  //! Create a simple test asset key
  [[nodiscard]] static auto CreateTestAssetKey(const std::string& name)
    -> data::AssetKey;

  std::vector<std::filesystem::path> generated_paks_;
};

} // namespace oxygen::content::testing

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal
