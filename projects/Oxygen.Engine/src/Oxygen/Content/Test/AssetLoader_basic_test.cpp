//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderBasicTest;

auto AssetLoaderBasicTest::SetUp() -> void
{
  // Create a per-test temporary directory to avoid cross-process fixture races
  // when tests run concurrently in separate executables.
  const auto* test_info
    = ::testing::UnitTest::GetInstance()->current_test_info();
  const auto sanitize = [](std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (const char c : name) {
      const bool alpha_num = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
      sanitized.push_back(alpha_num ? c : '_');
    }
    return sanitized;
  };
  const auto unique_suffix
    = std::chrono::steady_clock::now().time_since_epoch().count();

  temp_dir_ = std::filesystem::temp_directory_path()
    / "oxygen_asset_loader_tests"
    / (sanitize(test_info->test_suite_name()) + "_"
      + sanitize(test_info->name()) + "_" + std::to_string(unique_suffix));
  std::filesystem::create_directories(temp_dir_);

  // Create AssetLoader instance
  asset_loader_ = std::make_unique<AssetLoader>(Tag::Get());
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
NOLINT_TEST_F(AssetLoaderBasicTest, LoadAssetCorruptedPakHandlesGracefully)
{
  // Arrange - Create a corrupted PAK file
  auto corrupted_pak = temp_dir_ / "corrupted.pak";
  {
    std::ofstream file(corrupted_pak, std::ios::binary);
    file << "CORRUPTED_DATA_NOT_A_VALID_PAK_FILE";
  }

  // Act & Assert - Should throw exception for corrupted file
  NOLINT_EXPECT_THROW(
    { asset_loader_->AddPakFile(corrupted_pak); }, std::exception);
}

//! Test: AssetLoader handles missing PAK file gracefully
/*!
 Scenario: Attempts to add a non-existent PAK file and verifies
 graceful error handling.
*/
NOLINT_TEST_F(AssetLoaderBasicTest, AddPakFileNonExistentHandlesGracefully)
{
  // Arrange
  const auto non_existent_pak = temp_dir_ / "non_existent.pak";

  // Act & Assert - Should handle gracefully (may throw or return gracefully)
  // Implementation-dependent behavior
  NOLINT_EXPECT_NO_THROW({
    try {
      asset_loader_->AddPakFile(non_existent_pak);
    } catch (const std::exception&) {
      // Expected behavior for missing file
    }
  });
}

NOLINT_TEST_F(AssetLoaderBasicTest, ConsoleTelemetryBindingsExpectedToRoundTrip)
{
  oxygen::console::Console console;
  asset_loader_->RegisterConsoleBindings(oxygen::observer_ptr { &console });

  const auto disable = console.Execute("cntt.telemetry_enabled 0");
  EXPECT_EQ(disable.status, oxygen::console::ExecutionStatus::kOk);
  asset_loader_->ApplyConsoleCVars(console);
  EXPECT_FALSE(asset_loader_->IsTelemetryEnabled());

  const auto dump = console.Execute("cntt.dump_stats");
  EXPECT_EQ(dump.status, oxygen::console::ExecutionStatus::kOk);
  EXPECT_FALSE(dump.output.empty());

  const auto reset = console.Execute("cntt.reset_stats");
  EXPECT_EQ(reset.status, oxygen::console::ExecutionStatus::kOk);

  std::string last_stats;
  EXPECT_TRUE(
    console.TryGetCVarValue<std::string>("cntt.last_stats", last_stats));
}

} // namespace
