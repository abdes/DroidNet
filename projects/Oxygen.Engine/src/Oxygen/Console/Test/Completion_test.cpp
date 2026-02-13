//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Console.h>

namespace {

using oxygen::console::CommandContext;
using oxygen::console::CommandDefinition;
using oxygen::console::CommandFlags;
using oxygen::console::Console;
using oxygen::console::ExecutionResult;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsoleCompletion, RanksByFrequencyAndRecency)
{
  Console console {};

  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "r.reset",
        .help = "Reset",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "r.reload",
        .help = "Reload",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
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
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "sys.Exit",
        .help = "Exit",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
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
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "gfx.reload",
        .help = "Reload",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "gfx.reset",
        .help = "Reset",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
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
