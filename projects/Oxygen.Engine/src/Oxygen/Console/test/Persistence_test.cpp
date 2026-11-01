//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using nlohmann::json;
using oxygen::PathFinder;
using oxygen::commands::ExecutionStatus;
using oxygen::console::ArchiveSaveMode;
using oxygen::console::Console;
using oxygen::console::ConsoleStartupPlan;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::CVarRegistrationOptions;
using oxygen::console::CVarValueOrigin;
using oxygen::console::StampedCVarValue;

struct TestEnvironment {
  oxygen::testing::ScopedTempDir dir;
  std::shared_ptr<const oxygen::PathFinderConfig> config;
  PathFinder path_finder;

  TestEnvironment()
    : config(oxygen::PathFinderConfig::Create()
          .WithWorkspaceRoot(dir.Path())
          .WithCVarsArchivePath("console/cvars.json")
          .BuildShared())
    , path_finder(config, dir.Path())
  {
  }
};

auto MakeTestEnvironment() -> TestEnvironment { return TestEnvironment {}; }

auto WriteArchivedInt(const PathFinder& path_finder,
  const std::string_view name, const int64_t value) -> void
{
  json payload = json::object();
  payload["version"] = 1;
  payload["cvars"] = json::array({
    json::object({
      { "name", name },
      { "type", "Int" },
      { "value", value },
    }),
  });

  const auto archive_path = path_finder.CVarsArchivePath();
  std::filesystem::create_directories(archive_path.parent_path());
  std::ofstream stream(archive_path);
  stream << payload.dump(2);
}

auto ReadArchivedInt(const PathFinder& path_finder, const std::string_view name)
  -> std::optional<int64_t>
{
  std::ifstream stream(path_finder.CVarsArchivePath());
  if (!stream.is_open()) {
    return std::nullopt;
  }

  json payload;
  stream >> payload;
  if (!payload.is_object() || !payload.contains("cvars")
    || !payload["cvars"].is_array()) {
    return std::nullopt;
  }

  for (const auto& entry : payload["cvars"]) {
    if (!entry.is_object() || !entry.contains("name")
      || !entry["name"].is_string() || entry["name"].get<std::string>() != name
      || !entry.contains("value") || !entry["value"].is_number_integer()) {
      continue;
    }
    return entry["value"].get<int64_t>();
  }
  return std::nullopt;
}

auto RegisterArchivedInt(Console& console, const std::string_view name,
  const int64_t default_value, const CVarRegistrationOptions& options = {})
  -> void
{
  ASSERT_TRUE(console
      .RegisterCVar(
        CVarDefinition {
          .name = std::string(name),
          .help = "test cvar",
          .default_value = default_value,
          .flags = CVarFlags::kArchive,
          .min_value = std::nullopt,
          .max_value = std::nullopt,
        },
        options)
      .IsValid());
}

NOLINT_TEST(ConsolePersistence, PersistedPreferenceBeatsAppDefault)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1, CVarRegistrationOptions {
    .initial = StampedCVarValue {
      .value = int64_t { 5 },
      .origin = CVarValueOrigin::kAppDefault,
    },
  });

  EXPECT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 2);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kPersistedPreference);
}

NOLINT_TEST(
  ConsolePersistence, StartupExplicitBeatsPersistedPreferenceForSession)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);
  auto startup_plan = ConsoleStartupPlan {};
  startup_plan.Set("r.quality", int64_t { 7 });
  console.ApplyStartupPlan(startup_plan);

  EXPECT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 7);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kStartupExplicit);
}

NOLINT_TEST(ConsolePersistence, AutomaticSaveDoesNotPromoteStartupOnlyOverride)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);
  auto startup_plan = ConsoleStartupPlan {};
  startup_plan.Set("r.quality", int64_t { 7 });
  console.ApplyStartupPlan(startup_plan);
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);

  EXPECT_EQ(
    console.SaveArchiveCVars(env.path_finder, ArchiveSaveMode::kAutomatic)
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(ReadArchivedInt(env.path_finder, "r.quality"), 2);
}

NOLINT_TEST(ConsolePersistence, RuntimeExplicitOverwritesArchiveOnAutomaticSave)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("r.quality 9").status, ExecutionStatus::kOk);

  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kRuntimeExplicit);

  EXPECT_EQ(
    console.SaveArchiveCVars(env.path_finder, ArchiveSaveMode::kAutomatic)
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(ReadArchivedInt(env.path_finder, "r.quality"), 9);
}

NOLINT_TEST(ConsolePersistence, ExplicitSavePromotesCurrentLiveValue)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);
  auto startup_plan = ConsoleStartupPlan {};
  startup_plan.Set("r.quality", int64_t { 7 });
  console.ApplyStartupPlan(startup_plan);
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);

  EXPECT_EQ(
    console.SaveArchiveCVars(env.path_finder, ArchiveSaveMode::kExplicit)
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(ReadArchivedInt(env.path_finder, "r.quality"), 7);
}

NOLINT_TEST(
  ConsolePersistence, LateRegistrationAppliesCachedPersistedPreference)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  RegisterArchivedInt(console, "r.quality", 1);

  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 2);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kPersistedPreference);
}

NOLINT_TEST(
  ConsolePersistence, LateRegistrationPreservesHigherPrecedenceStartupExplicit)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  auto startup_plan = ConsoleStartupPlan {};
  startup_plan.Set("r.quality", int64_t { 7 });
  console.ApplyStartupPlan(startup_plan);
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  RegisterArchivedInt(console, "r.quality", 1);

  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 7);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kStartupExplicit);
}

NOLINT_TEST(ConsolePersistence, CommandLineOverridesUseStartupExplicitOrigin)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);

  const std::vector<std::string_view> args { "+r.quality=4" };
  const auto result = console.ApplyCommandLineOverrides(args);
  EXPECT_EQ(result.status, ExecutionStatus::kOk);

  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 4);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kStartupExplicit);
}

NOLINT_TEST(ConsolePersistence, ExplicitSaveDoesNotPromoteAppForcedValue)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  RegisterArchivedInt(console, "r.quality", 1, CVarRegistrationOptions {
    .initial = StampedCVarValue {
      .value = int64_t { 11 },
      .origin = CVarValueOrigin::kAppForced,
    },
  });

  const auto snapshot = console.FindCVar("r.quality");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 11);
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kAppForced);

  EXPECT_EQ(
    console.SaveArchiveCVars(env.path_finder, ArchiveSaveMode::kExplicit)
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(ReadArchivedInt(env.path_finder, "r.quality"), 2);
}

//! A save replaces the archive while a reader holds the old one open, sharing
//! deletion; the reader keeps the bytes it opened.
NOLINT_TEST(ConsolePersistence, SaveReplacesAnArchiveAReaderHasOpen)
{
  auto env = MakeTestEnvironment();
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  RegisterArchivedInt(console, "r.quality", 1);
  ASSERT_EQ(
    console.LoadArchiveCVars(env.path_finder).status, ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("r.quality 9").status, ExecutionStatus::kOk);

  const auto reader
    = oxygen::serio::OpenPublishedFile(env.path_finder.CVarsArchivePath());
  ASSERT_TRUE(reader.has_value());

  EXPECT_EQ(
    console.SaveArchiveCVars(env.path_finder, ArchiveSaveMode::kAutomatic)
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(ReadArchivedInt(env.path_finder, "r.quality"), 9);
}

} // namespace
