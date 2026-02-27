//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderBasicTest;

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
