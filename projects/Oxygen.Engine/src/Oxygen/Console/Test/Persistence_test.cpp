//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>

namespace {

using nlohmann::json;
using oxygen::PathFinder;
using oxygen::console::ArchiveSaveMode;
using oxygen::console::Console;
using oxygen::console::ConsoleStartupPlan;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::CVarRegistrationOptions;
using oxygen::console::CVarValueOrigin;
using oxygen::console::ExecutionStatus;
using oxygen::console::StampedCVarValue;

struct TestEnvironment {
  std::filesystem::path root;
  std::shared_ptr<const oxygen::PathFinderConfig> config;
  PathFinder path_finder;

  TestEnvironment(std::filesystem::path root_in,
    std::shared_ptr<const oxygen::PathFinderConfig> config_in,
    PathFinder path_finder_in)
    : root(std::move(root_in))
    , config(std::move(config_in))
    , path_finder(std::move(path_finder_in))
  {
  }

  ~TestEnvironment() { std::filesystem::remove_all(root); }

  TestEnvironment(const TestEnvironment&) = delete;
  auto operator=(const TestEnvironment&) -> TestEnvironment& = delete;
  TestEnvironment(TestEnvironment&&) = default;
  auto operator=(TestEnvironment&&) -> TestEnvironment& = default;
};

auto MakeTestEnvironment(const std::string_view name) -> TestEnvironment
{
  const auto root = std::filesystem::temp_directory_path()
    / "oxygen_console_persistence" / std::string(name);
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto config = oxygen::PathFinderConfig::Create()
                  .WithWorkspaceRoot(root)
                  .WithCVarsArchivePath("console/cvars.json")
                  .BuildShared();
  return TestEnvironment {
    root,
    config,
    PathFinder { config, root },
  };
}

auto WriteArchivedInt(const PathFinder& path_finder,
  const std::string_view name, const int64_t value) -> void
{
  json payload = json::object();
  payload["version"] = 1;
  payload["cvars"] = json::array({ json::object({
    { "name", name },
    { "type", "Int" },
    { "value", value },
  }) });

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
  auto env = MakeTestEnvironment("persisted_preference_beats_app_default");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("startup_explicit_beats_persisted");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("automatic_save_does_not_promote_startup");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("runtime_explicit_overwrites_archive");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("explicit_save_promotes_live_value");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("late_registration_persisted_preference");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  auto env = MakeTestEnvironment("late_registration_startup_explicit");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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
  Console console {};
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
  auto env = MakeTestEnvironment("explicit_save_does_not_promote_app_forced");
  WriteArchivedInt(env.path_finder, "r.quality", 2);

  Console console {};
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

} // namespace
