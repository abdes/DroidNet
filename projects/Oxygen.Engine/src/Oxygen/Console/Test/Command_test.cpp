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

NOLINT_TEST(ConsoleCommand, ExecutesRegisteredHandler)
{
  Console console {};
  bool called = false;
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "test.cmd",
        .help = "Test command",
        .flags = CommandFlags::kNone,
        .handler = [&called](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          called = true;
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  EXPECT_EQ(console.Execute("test.cmd").status, ExecutionStatus::kOk);
  EXPECT_TRUE(called);
}

NOLINT_TEST(ConsoleCommand, PassesArgumentsToHandler)
{
  Console console {};
  std::vector<std::string> captured_args;
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "test.args",
        .help = "Args test",
        .flags = CommandFlags::kNone,
        .handler = [&captured_args](const std::vector<std::string>& args,
                     const CommandContext&) -> ExecutionResult {
          captured_args = args;
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  EXPECT_EQ(
    console.Execute("test.args val1 \"val 2\"").status, ExecutionStatus::kOk);
  ASSERT_EQ(captured_args.size(), 2);
  EXPECT_EQ(captured_args[0], "val1");
  EXPECT_EQ(captured_args[1], "val 2");
}

NOLINT_TEST(ConsoleCommand, PropagatesErrorHandlerStatus)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "test.fail",
        .help = "Failure test",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kError,
            .exit_code = 1,
            .output = {},
            .error = "intentional failure" };
        },
      })
      .IsValid());

  const auto result = console.Execute("test.fail");
  EXPECT_EQ(result.status, ExecutionStatus::kError);
  EXPECT_EQ(result.error, "intentional failure");
}

NOLINT_TEST(ConsoleCommand, RejectsUnregisteredCommands)
{
  Console console {};
  const auto result = console.Execute("unknown.command");
  EXPECT_EQ(result.status, ExecutionStatus::kNotFound);
}

} // namespace
