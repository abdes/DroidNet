//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::testing::AssetLoaderBasicTest;

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

namespace {

NOLINT_TEST_F(AssetLoaderBasicTest, TruncatedPakLeavesMountsUnchanged)
{
  const auto path = temp_dir_ / "truncated.pak";
  {
    std::ofstream file(path, std::ios::binary);
    file << "NOT_A_PAK";
  }
  try {
    asset_loader_->AddPakFile(path);
    FAIL() << "Truncated header was accepted";
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "Failed to read pak header");
  }
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
}

NOLINT_TEST_F(AssetLoaderBasicTest, MissingPakLeavesMountsUnchanged)
{
  try {
    asset_loader_->AddPakFile(temp_dir_ / "missing.pak");
    FAIL() << "Missing source was mounted";
  } catch (const std::system_error& error) {
    EXPECT_EQ(error.code(), std::errc::no_such_file_or_directory);
  }
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
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
