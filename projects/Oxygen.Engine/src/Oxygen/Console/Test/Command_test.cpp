//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

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

NOLINT_TEST(ConsoleCommand, RetirementRemovesDiscoveryAndRejectsStaleHandles)
{
  Console console {};
  const auto make_command = [](const char* output) {
    return CommandDefinition {
      .name = "test.retired",
      .help = "Retirement test",
      .flags = CommandFlags::kNone,
      .handler = [output](const auto&, const auto&) -> ExecutionResult {
        return { .output = output };
      },
    };
  };
  const auto first = console.RegisterCommand(make_command("first"));
  ASSERT_TRUE(first.IsValid());
  ASSERT_NE(console.BeginCompletionCycle("test.retired"), nullptr);
  EXPECT_TRUE(console.UnregisterCommand(first));
  EXPECT_EQ(console.CurrentCompletion(), nullptr);
  EXPECT_TRUE(console.Complete("test.retired").empty());
  EXPECT_EQ(console.Execute("test.retired").status, ExecutionStatus::kNotFound);
  const auto symbols = console.ListSymbols();
  EXPECT_TRUE(std::ranges::none_of(symbols,
    [](const auto& symbol) { return symbol.token == "test.retired"; }));
  EXPECT_FALSE(console.UnregisterCommand(first));
  EXPECT_FALSE(console.UnregisterCommand({}));
  const auto second = console.RegisterCommand(make_command("second"));
  ASSERT_TRUE(second.IsValid());
  EXPECT_NE(first.id, second.id);
  EXPECT_FALSE(console.UnregisterCommand(first));
  EXPECT_EQ(console.Execute("test.retired").output, "second");
}

NOLINT_TEST(ConsoleCommand, RepeatedExecutionPreservesMutableCallableState)
{
  Console console {};
  const auto handle = console.RegisterCommand({
    .name = "test.counter",
    .help = "Mutable command state",
    .flags = CommandFlags::kNone,
    .handler = [calls = 0](const auto&, const auto&) mutable
      -> ExecutionResult { return { .output = std::to_string(++calls) }; },
  });
  ASSERT_TRUE(handle.IsValid());
  const auto context = CommandContext {
    .source = oxygen::console::CommandSource::kAutomation,
    .shipping_build = true,
  };
  EXPECT_EQ(console.Execute("test.counter", context).output, "1");
  EXPECT_EQ(console.Execute("test.counter", context).output, "2");
  EXPECT_TRUE(console.UnregisterCommand(handle));
}

NOLINT_TEST(ConsoleCommand, SelfRetirementPinsCallableAndAllowsReplacement)
{
  Console console {};
  oxygen::console::CommandHandle original;
  oxygen::console::CommandHandle replacement;
  auto payload = std::make_shared<int>(42);
  const auto weak = std::weak_ptr<int>(payload);
  original = console.RegisterCommand({
    .name = "test.replace",
    .help = "Original command",
    .flags = CommandFlags::kNone,
    .handler = [&, owner = std::move(payload)](
                 const auto&, const auto&) -> ExecutionResult {
      EXPECT_TRUE(console.UnregisterCommand(original));
      EXPECT_FALSE(weak.expired());
      EXPECT_EQ(*owner, 42);
      replacement = console.RegisterCommand({
        .name = "test.replace",
        .help = "Replacement command",
        .flags = CommandFlags::kNone,
        .handler = [](const auto&, const auto&) -> ExecutionResult {
          return { .output = "replacement" };
        },
      });
      return { .output = "original" };
    },
  });
  ASSERT_TRUE(original.IsValid());
  EXPECT_EQ(console.Execute("test.replace").output, "original");
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(replacement.IsValid());
  EXPECT_FALSE(console.UnregisterCommand(original));
  const auto symbols = console.ListSymbols();
  const auto found = std::ranges::find_if(
    symbols, [](const auto& symbol) { return symbol.token == "test.replace"; });
  ASSERT_NE(found, symbols.end());
  EXPECT_EQ(found->usage_frequency, 0U);
  EXPECT_EQ(console.Execute("test.replace").output, "replacement");
}

} // namespace
