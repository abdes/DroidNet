//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::commands::CommandContext;
using oxygen::commands::CommandDefinition;
using oxygen::commands::CommandFlags;
using oxygen::commands::CommandResult;
using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;

NOLINT_TEST(ConsoleCompletion, RanksByFrequencyAndRecency)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };

  ASSERT_TRUE(command_registry
      .Register(CommandDefinition {
        .name = "r.reset",
        .help = "Reset",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  ASSERT_TRUE(command_registry
      .Register(CommandDefinition {
        .name = "r.reload",
        .help = "Reload",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  EXPECT_EQ(console.Execute("r.reload").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("r.reset").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("r.reload").status, ExecutionStatus::kOk);

  const auto completions = console.Complete("r.re");
  ASSERT_GE(completions.size(), 2);
  EXPECT_EQ(completions[0].token, "r.reload");
  EXPECT_EQ(completions[1].token, "r.reset");
}

NOLINT_TEST(ConsoleCompletion, IsCaseInsensitive)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_TRUE(command_registry
      .Register(CommandDefinition {
        .name = "sys.Exit",
        .help = "Exit",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  auto completions = console.Complete("SYS.EX");
  ASSERT_EQ(completions.size(), 1);
  EXPECT_EQ(completions[0].token, "sys.Exit");

  completions = console.Complete("sys.ex");
  ASSERT_EQ(completions.size(), 1);
  EXPECT_EQ(completions[0].token, "sys.Exit");
}

NOLINT_TEST(ConsoleCompletion, SupportsCyclingState)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_TRUE(command_registry
      .Register(CommandDefinition {
        .name = "gfx.reload",
        .help = "Reload",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());
  ASSERT_TRUE(command_registry
      .Register(CommandDefinition {
        .name = "gfx.reset",
        .help = "Reset",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  const auto start = console.BeginCompletionCycle("gfx.re");
  ASSERT_NE(start, nullptr);

  const auto next = console.NextCompletion();
  ASSERT_NE(next, nullptr);
  const auto wrapped = console.NextCompletion();
  ASSERT_NE(wrapped, nullptr);
  const auto previous = console.PreviousCompletion();
  ASSERT_NE(previous, nullptr);

  EXPECT_EQ(previous->token, next->token);
}

} // namespace
