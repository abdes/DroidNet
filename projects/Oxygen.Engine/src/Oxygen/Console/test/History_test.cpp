//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;

NOLINT_TEST(ConsoleHistory, RecordsExecutedLines)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("list commands").status, ExecutionStatus::kOk);

  const auto& entries = console.GetHistory().Entries();
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0], "help");
  EXPECT_EQ(entries[1], "list commands");
}

NOLINT_TEST(ConsoleHistory, DoesNotRecordDuplicateConsecutiveLines)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);

  const auto& entries = console.GetHistory().Entries();
  EXPECT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0], "help");
}

//! Only lines typed in the console are history.
NOLINT_TEST(ConsoleHistory, RecordsOnlyLocalConsoleLines)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console
              .Execute("list commands",
                oxygen::commands::CommandContext {
                  .source = oxygen::commands::CommandSource::kAutomation,
                })
              .status,
    ExecutionStatus::kOk);

  const auto& entries = console.GetHistory().Entries();
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0], "help");
}

NOLINT_TEST(ConsoleHistory, RespectsCapacity)
{
  // Minimum capacity is enforced as 32 in History.cpp
  constexpr size_t kHistoryCapacity = 32;
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry },
    kHistoryCapacity };

  // Register commands so they don't fail with kNotFound
  constexpr int kTotalCommands = 40;
  for (int i = 0; i < kTotalCommands; ++i) {
    ASSERT_TRUE(command_registry
        .Register({ .name = "cmd" + std::to_string(i),
          .help = "dummy",
          .flags = oxygen::commands::CommandFlags::kNone,
          .handler = [](const auto&, const auto&) -> auto {
            return oxygen::commands::CommandResult { .status
              = oxygen::commands::ExecutionStatus::kOk,
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
    oxygen::commands::CommandRegistry writer_commands;
    Console writer { oxygen::observer_ptr { &writer_commands } };
    ASSERT_TRUE(writer_commands
        .Register({ .name = "cmd1",
          .help = "dummy",
          .flags = oxygen::commands::CommandFlags::kNone,
          .handler = [](const auto&, const auto&) -> auto {
            return oxygen::commands::CommandResult { .status
              = oxygen::commands::ExecutionStatus::kOk,
              .output = {},
              .error = {} };
          } })
        .IsValid());
    ASSERT_TRUE(writer_commands
        .Register({ .name = "cmd2",
          .help = "dummy",
          .flags = oxygen::commands::CommandFlags::kNone,
          .handler = [](const auto&, const auto&) -> auto {
            return oxygen::commands::CommandResult { .status
              = oxygen::commands::ExecutionStatus::kOk,
              .output = {},
              .error = {} };
          } })
        .IsValid());

    EXPECT_EQ(writer.Execute("cmd1").status, ExecutionStatus::kOk);
    EXPECT_EQ(writer.Execute("cmd2").status, ExecutionStatus::kOk);
    EXPECT_EQ(writer.SaveHistory(path_finder).status, ExecutionStatus::kOk);
  }

  {
    oxygen::commands::CommandRegistry reader_commands;
    Console reader { oxygen::observer_ptr { &reader_commands } };
    EXPECT_EQ(reader.LoadHistory(path_finder).status, ExecutionStatus::kOk);
    const auto& entries = reader.GetHistory().Entries();
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0], "cmd1");
    EXPECT_EQ(entries[1], "cmd2");
  }

  std::filesystem::remove_all(temp_root);
}

} // namespace
