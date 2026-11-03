//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <span>
#include <string_view>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::testing::AssetLoaderBasicTest;

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

namespace {

NOLINT_TEST_F(AssetLoaderBasicTest, TruncatedPakLeavesMountsUnchanged)
{
  const auto path = TempDir() / "truncated.pak";
  constexpr std::string_view kNotAPak = "NOT_A_PAK";
  oxygen::cooker::testing::Materialize(
    std::as_bytes(std::span(kNotAPak.data(), kNotAPak.size())), path);
  try {
    asset_loader_->AddPakFile(path);
    FAIL() << "Truncated header was accepted";
  } catch (const oxygen::data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), oxygen::data::CookedDefect::kTooSmall);
  }
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
}

NOLINT_TEST_F(AssetLoaderBasicTest, MissingPakLeavesMountsUnchanged)
{
  try {
    asset_loader_->AddPakFile(TempDir() / "missing.pak");
    FAIL() << "Missing source was mounted";
  } catch (const oxygen::data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), oxygen::data::CookedDefect::kAbsent);
  }
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
}

//! A folder that holds no loose cooked index is refused as absent, as a
//! missing pak is.
NOLINT_TEST_F(AssetLoaderBasicTest, MissingLooseRootLeavesMountsUnchanged)
{
  const auto empty = TempDir() / "empty";
  std::filesystem::create_directories(empty);

  try {
    asset_loader_->AddLooseCookedRoot(empty);
    FAIL() << "A folder without an index was mounted";
  } catch (const oxygen::data::CookedFormatError& error) {
    EXPECT_EQ(error.Defect(), oxygen::data::CookedDefect::kAbsent);
  }
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
}

NOLINT_TEST_F(AssetLoaderBasicTest, ConsoleTelemetryBindingsExpectedToRoundTrip)
{
  oxygen::commands::CommandRegistry command_registry;
  oxygen::console::Console console { oxygen::observer_ptr {
    &command_registry } };
  asset_loader_->RegisterConsoleBindings(oxygen::observer_ptr { &console });
  asset_loader_->RegisterCommands(oxygen::observer_ptr { &command_registry });

  const auto disable = console.Execute("cntt.telemetry_enabled 0");
  EXPECT_EQ(disable.status, oxygen::commands::ExecutionStatus::kOk);
  asset_loader_->ApplyConsoleCVars(console);
  EXPECT_FALSE(asset_loader_->IsTelemetryEnabled());

  const auto dump = console.Execute("cntt.dump_stats");
  EXPECT_EQ(dump.status, oxygen::commands::ExecutionStatus::kOk);
  EXPECT_FALSE(dump.output.empty());

  const auto reset = console.Execute("cntt.reset_stats");
  EXPECT_EQ(reset.status, oxygen::commands::ExecutionStatus::kOk);

  std::string last_stats;
  EXPECT_TRUE(
    console.TryGetCVarValue<std::string>("cntt.last_stats", last_stats));
}

} // namespace
