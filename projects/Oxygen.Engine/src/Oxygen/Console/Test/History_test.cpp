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

using oxygen::console::Console;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsoleHistory, RecordsExecutedLines)
{
  Console console {};
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("list commands").status, ExecutionStatus::kOk);

  const auto& entries = console.GetHistory().Entries();
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0], "help");
  EXPECT_EQ(entries[1], "list commands");
}

NOLINT_TEST(ConsoleHistory, DoesNotRecordDuplicateConsecutiveLines)
{
  Console console {};
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);

  const auto& entries = console.GetHistory().Entries();
  EXPECT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0], "help");
}

NOLINT_TEST(ConsoleHistory, RespectsCapacity)
{
  // Minimum capacity is enforced as 32 in History.cpp
  constexpr size_t kHistoryCapacity = 32;
  Console console { kHistoryCapacity };

  // Register commands so they don't fail with kNotFound
  constexpr int kTotalCommands = 40;
  for (int i = 0; i < kTotalCommands; ++i) {
    ASSERT_TRUE(console
        .RegisterCommand({ .name = "cmd" + std::to_string(i),
          .help = "dummy",
          .flags = oxygen::console::CommandFlags::kNone,
          .handler =
            [](const auto&, const auto&) {
              return oxygen::console::ExecutionResult { .status
                = oxygen::console::ExecutionStatus::kOk,
                .exit_code = 0,
                .output = {},
                .error = {} };
            } })
        .IsValid());
  }

  for (int i = 0; i < kTotalCommands; ++i) {
    EXPECT_EQ(
      console.Execute("cmd" + std::to_string(i)).status, ExecutionStatus::kOk);
  }

  const auto& entries = console.GetHistory().Entries();
  EXPECT_EQ(entries.size(), kHistoryCapacity);
  EXPECT_EQ(entries.front(), "cmd8");
  EXPECT_EQ(entries.back(), "cmd39");
}

NOLINT_TEST(ConsoleHistory, PersistsAcrossSessions)
{
  const auto temp_root
    = std::filesystem::temp_directory_path() / "oxygen_history_test";
  std::filesystem::create_directories(temp_root);

  auto config = oxygen::PathFinderConfig::Create()
                  .WithWorkspaceRoot(temp_root)
                  .WithCVarsArchivePath("console/cvars.json")
                  .BuildShared();
  oxygen::PathFinder path_finder { config, temp_root };

  {
    Console writer {};
    ASSERT_TRUE(writer
        .RegisterCommand({ .name = "cmd1",
          .help = "dummy",
          .flags = oxygen::console::CommandFlags::kNone,
          .handler =
            [](const auto&, const auto&) {
              return oxygen::console::ExecutionResult { .status
                = oxygen::console::ExecutionStatus::kOk,
                .exit_code = 0,
                .output = {},
                .error = {} };
            } })
        .IsValid());
    ASSERT_TRUE(writer
        .RegisterCommand({ .name = "cmd2",
          .help = "dummy",
          .flags = oxygen::console::CommandFlags::kNone,
          .handler =
            [](const auto&, const auto&) {
              return oxygen::console::ExecutionResult { .status
                = oxygen::console::ExecutionStatus::kOk,
                .exit_code = 0,
                .output = {},
                .error = {} };
            } })
        .IsValid());

    EXPECT_EQ(writer.Execute("cmd1").status, ExecutionStatus::kOk);
    EXPECT_EQ(writer.Execute("cmd2").status, ExecutionStatus::kOk);
    EXPECT_EQ(writer.SaveHistory(path_finder).status, ExecutionStatus::kOk);
  }

  {
    Console reader {};
    EXPECT_EQ(reader.LoadHistory(path_finder).status, ExecutionStatus::kOk);
    const auto& entries = reader.GetHistory().Entries();
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0], "cmd1");
    EXPECT_EQ(entries[1], "cmd2");
  }

  std::filesystem::remove_all(temp_root);
}

} // namespace
