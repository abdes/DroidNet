//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>

namespace {

using oxygen::PathFinder;
using oxygen::console::Console;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsolePersistence, PersistsAndLoadsArchiveCVars)
{
  const auto temp_root
    = std::filesystem::temp_directory_path() / "oxygen_persistence_test";
  std::filesystem::create_directories(temp_root);

  auto config = oxygen::PathFinderConfig::Create()
                  .WithWorkspaceRoot(temp_root)
                  .WithCVarsArchivePath("console/cvars.json")
                  .BuildShared();
  PathFinder path_finder { config, temp_root };

  {
    Console writer {};
    ASSERT_TRUE(writer
        .RegisterCVar({
          .name = "r.archived",
          .help = "Archived",
          .default_value = int64_t { 1 },
          .flags = CVarFlags::kArchive,
          .min_value = std::nullopt,
          .max_value = std::nullopt,
        })
        .IsValid());
    ASSERT_TRUE(writer
        .RegisterCVar({
          .name = "r.volatile",
          .help = "Volatile",
          .default_value = int64_t { 1 },
          .flags = CVarFlags::kNone,
          .min_value = std::nullopt,
          .max_value = std::nullopt,
        })
        .IsValid());

    EXPECT_EQ(writer.Execute("r.archived 0").status, ExecutionStatus::kOk);
    EXPECT_EQ(writer.Execute("r.volatile 0").status, ExecutionStatus::kOk);
    EXPECT_EQ(
      writer.SaveArchiveCVars(path_finder).status, ExecutionStatus::kOk);
  }

  {
    Console reader {};
    ASSERT_TRUE(reader
        .RegisterCVar({
          .name = "r.archived",
          .help = "Archived",
          .default_value = int64_t { 1 },
          .flags = CVarFlags::kArchive,
          .min_value = std::nullopt,
          .max_value = std::nullopt,
        })
        .IsValid());
    ASSERT_TRUE(reader
        .RegisterCVar({
          .name = "r.volatile",
          .help = "Volatile",
          .default_value = int64_t { 1 },
          .flags = CVarFlags::kNone,
          .min_value = std::nullopt,
          .max_value = std::nullopt,
        })
        .IsValid());

    EXPECT_EQ(
      reader.LoadArchiveCVars(path_finder).status, ExecutionStatus::kOk);
    EXPECT_EQ(
      std::get<int64_t>(reader.FindCVar("r.archived")->current_value), 0);
    EXPECT_EQ(
      std::get<int64_t>(reader.FindCVar("r.volatile")->current_value), 1);
  }

  std::filesystem::remove_all(temp_root);
}

NOLINT_TEST(ConsolePersistence, AppliesCommandLineOverrides)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "r.quality",
        .help = "Quality",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  const std::vector<std::string_view> args { "+r.quality=4" };
  const auto result = console.ApplyCommandLineOverrides(args);
  EXPECT_EQ(result.status, ExecutionStatus::kOk);
  EXPECT_EQ(std::get<int64_t>(console.FindCVar("r.quality")->current_value), 4);
}

} // namespace
