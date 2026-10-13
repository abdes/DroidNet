//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::observer_ptr;
using oxygen::commands::CommandArgs;
using oxygen::commands::CommandContext;
using oxygen::commands::CommandDefinition;
using oxygen::commands::CommandHandle;
using oxygen::commands::CommandParam;
using oxygen::commands::CommandRegistry;
using oxygen::commands::CommandResult;
using oxygen::commands::CommandSource;
using oxygen::commands::ExecutionStatus;
using oxygen::commands::ParamType;
using oxygen::commands::ParamValue;
using oxygen::console::Console;
using oxygen::console::CVarDefinition;

//! A console over its own command registry.
class ConsoleCommandTest : public testing::Test {
protected:
  CommandRegistry registry_;
  Console console_ { observer_ptr { &registry_ } };
};

//! `test.echo text [count]` repeats its text.
auto EchoCommand() -> CommandDefinition
{
  return CommandDefinition {
    .name = "test.echo",
    .label = "Echo",
    .help = "Echo text",
    .params = {
      CommandParam {
        .name = "text",
        .type = ParamType::kString,
        .required = true,
      },
      CommandParam {
        .name = "count",
        .type = ParamType::kInt,
        .default_value = ParamValue { int64_t { 1 } },
        .min_value = 1.0,
      },
    },
    .handler = [](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      auto output = std::string {};
      for (int64_t i = 0; i < args.GetInt("count"); ++i) {
        output += args.GetString("text");
      }
      return CommandResult::Success(output);
    },
  };
}

//! A console line runs a registered command with typed, quoted and named
//! arguments.
NOLINT_TEST_F(ConsoleCommandTest, LineRunsCommandWithTypedArguments)
{
  ASSERT_NE(registry_.Register(EchoCommand()).get(), 0U);

  const auto positional = console_.Execute("test.echo \"a b\" 2");
  const auto named = console_.Execute("test.echo count=3 x");

  EXPECT_EQ(positional.status, ExecutionStatus::kOk);
  EXPECT_EQ(positional.output, "a ba b");
  EXPECT_EQ(named.output, "xxx");
}

//! Bad arguments, unknown names and handler failures are reported as results.
NOLINT_TEST_F(ConsoleCommandTest, ReportsFailures)
{
  static_cast<void>(registry_.Register(EchoCommand()));
  static_cast<void>(registry_.Register(CommandDefinition {
    .name = "test.fail",
    .label = "Fail",
    .help = "Always fails",
    .handler = [](const CommandArgs&, const CommandContext&) -> CommandResult {
      return CommandResult::Failure(
        ExecutionStatus::kError, "intentional failure");
    },
  }));

  EXPECT_EQ(console_.Execute("test.echo x 0").status,
    ExecutionStatus::kInvalidArguments);
  EXPECT_EQ(
    console_.Execute("unknown.command").status, ExecutionStatus::kNotFound);
  const auto failed = console_.Execute("test.fail");
  EXPECT_EQ(failed.status, ExecutionStatus::kError);
  EXPECT_EQ(failed.error, "intentional failure");
}

//! A variable name alone reads it and with a value sets it, through the
//! `cvar.get` and `cvar.set` commands.
NOLINT_TEST_F(ConsoleCommandTest, VariableShorthandUsesCVarCommands)
{
  ASSERT_TRUE(console_
      .RegisterCVar(CVarDefinition {
        .name = "test.size",
        .help = "Size",
        .default_value = int64_t { 4 },
      })
      .IsValid());

  const auto set = console_.Execute("test.size 8");
  const auto get = console_.Execute("test.size");
  const auto parsed = console_.Parse("test.size 9");

  EXPECT_EQ(set.status, ExecutionStatus::kOk);
  EXPECT_EQ(get.output, "test.size = 8");
  EXPECT_EQ(get.data["value"], 8);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed.value().size(), 1U);
  EXPECT_EQ(parsed.value()[0].name, "cvar.set");
}

//! A line run synchronously reports an asynchronous command as pending
//! without running it.
NOLINT_TEST_F(ConsoleCommandTest, AsynchronousCommandIsPendingInExecute)
{
  bool ran = false;
  static_cast<void>(registry_.Register(CommandDefinition {
    .name = "test.later",
    .label = "Later",
    .help = "Runs across frames",
    .async_handler
    = [&ran](CommandArgs, CommandContext) -> oxygen::co::Co<CommandResult> {
      ran = true;
      co_return CommandResult::Success();
    },
  }));

  const auto result = console_.Execute("test.later");

  EXPECT_EQ(result.status, ExecutionStatus::kPending);
  EXPECT_FALSE(ran);
}

//! Only lines typed in the console enter its history.
NOLINT_TEST_F(ConsoleCommandTest, HistoryRecordsOnlyLocalConsoleLines)
{
  static_cast<void>(registry_.Register(EchoCommand()));

  static_cast<void>(console_.Execute("test.echo typed"));
  static_cast<void>(console_.Execute("test.echo scripted",
    CommandContext { .source = CommandSource::kAutomation }));

  EXPECT_EQ(console_.GetHistory().Entries().size(), 1U);
}

//! The console stops offering a command once it is unregistered, and runs a
//! replacement registered under the same name.
NOLINT_TEST_F(ConsoleCommandTest, FollowsRegistrationChanges)
{
  auto first = EchoCommand();
  first.name = "test.retired";
  const auto handle = registry_.Register(first);
  ASSERT_NE(handle.get(), 0U);

  EXPECT_TRUE(registry_.Unregister(handle));
  EXPECT_TRUE(console_.Complete("test.retired").empty());
  EXPECT_EQ(
    console_.Execute("test.retired x").status, ExecutionStatus::kNotFound);
  EXPECT_TRUE(std::ranges::none_of(console_.ListSymbols(),
    [](const auto& symbol) -> bool { return symbol.token == "test.retired"; }));

  ASSERT_NE(registry_.Register(first).get(), 0U);
  EXPECT_EQ(console_.Execute("test.retired x").output, "x");
}

//! A command may unregister itself while running; its call completes and a
//! replacement registered during the call runs next.
NOLINT_TEST_F(ConsoleCommandTest, SelfRetirementKeepsTheRunningCall)
{
  auto payload = std::make_shared<int>(42);
  const auto weak = std::weak_ptr<int>(payload);
  auto original = CommandHandle {};
  original = registry_.Register(CommandDefinition {
    .name = "test.replace",
    .label = "Replace",
    .help = "Replaces itself",
    .handler = [&, owner = std::move(payload)](
                 const CommandArgs&, const CommandContext&) -> CommandResult {
      EXPECT_TRUE(registry_.Unregister(original));
      EXPECT_EQ(*owner, 42);
      static_cast<void>(registry_.Register(CommandDefinition {
        .name = "test.replace",
        .label = "Replace",
        .help = "Replacement",
        .handler = [](const CommandArgs&, const CommandContext&)
          -> CommandResult { return CommandResult::Success("replacement"); },
      }));
      return CommandResult::Success("original");
    },
  });
  ASSERT_NE(original.get(), 0U);

  EXPECT_EQ(console_.Execute("test.replace").output, "original");
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(console_.Execute("test.replace").output, "replacement");
}

//! The console's built-ins are registered with it and removed with it.
NOLINT_TEST(ConsoleCommand, BuiltinsLiveAsLongAsTheConsole)
{
  CommandRegistry registry;
  {
    const Console console { observer_ptr { &registry } };
    EXPECT_NE(registry.Find("help"), nullptr);
    EXPECT_NE(registry.Find("cvar.set"), nullptr);
  }
  EXPECT_EQ(registry.Find("help"), nullptr);
  EXPECT_EQ(registry.Find("cvar.set"), nullptr);
  EXPECT_NE(registry.Find("cmd.list"), nullptr);
}

} // namespace
