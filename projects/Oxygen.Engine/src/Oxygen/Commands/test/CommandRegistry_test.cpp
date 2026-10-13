//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::commands::CommandArgs;
using oxygen::commands::CommandAvailability;
using oxygen::commands::CommandContext;
using oxygen::commands::CommandDefinition;
using oxygen::commands::CommandFlags;
using oxygen::commands::CommandParam;
using oxygen::commands::CommandRegistry;
using oxygen::commands::CommandResult;
using oxygen::commands::CommandSource;
using oxygen::commands::ExecutionStatus;
using oxygen::commands::ParamType;
using oxygen::commands::ParamValue;

//! `test.add a b` returns their sum as text and data.
auto AddCommand() -> CommandDefinition
{
  return CommandDefinition {
    .name = "test.add",
    .label = "Add",
    .help = "Add two integers",
    .params = {
      CommandParam { .name = "a", .type = ParamType::kInt, .required = true },
      CommandParam { .name = "b", .type = ParamType::kInt, .required = true },
    },
    .handler = [](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto sum = args.GetInt("a") + args.GetInt("b");
      return CommandResult::Success(
        std::to_string(sum), json { { "sum", sum } });
    },
  };
}

//! A command bound from text runs synchronously and returns data.
NOLINT_TEST(CommandRegistry, ExecutesBoundInvocation)
{
  CommandRegistry registry;
  ASSERT_NE(registry.Register(AddCommand()).get(), 0U);
  const auto tokens = std::vector<std::string> { "2", "3" };

  auto invocation = registry.BindText("test.add", tokens);
  ASSERT_TRUE(invocation);
  const auto result = registry.Execute(invocation.value(), {});

  EXPECT_EQ(result.status, ExecutionStatus::kOk);
  EXPECT_EQ(result.output, "5");
  EXPECT_EQ(result.data["sum"], 5);
}

//! Binding reports unknown commands and bad arguments as results.
NOLINT_TEST(CommandRegistry, BindFailuresAreResults)
{
  CommandRegistry registry;
  static_cast<void>(registry.Register(AddCommand()));

  const auto unknown = registry.BindJson("test.nope", json::object());
  const auto invalid = registry.BindJson("test.add", json { { "a", "x" } });

  ASSERT_FALSE(unknown);
  EXPECT_EQ(unknown.error().status, ExecutionStatus::kNotFound);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().status, ExecutionStatus::kInvalidArguments);
  EXPECT_EQ(invalid.error().data["errors"].size(), 2U);
}

//! A duplicate name or a definition without exactly one handler is refused.
NOLINT_TEST(CommandRegistry, RefusesInvalidRegistrations)
{
  CommandRegistry registry;
  static_cast<void>(registry.Register(AddCommand()));
  auto no_handler = AddCommand();
  no_handler.name = "test.none";
  no_handler.handler = {};

  EXPECT_EQ(registry.Register(AddCommand()).get(), 0U);
  EXPECT_EQ(registry.Register(no_handler).get(), 0U);
}

//! An unavailable command is refused with its reason, and `Describe` shows
//! the same reason.
NOLINT_TEST(CommandRegistry, DisabledCommandReportsReason)
{
  CommandRegistry registry;
  auto command = AddCommand();
  command.availability = [](const CommandContext&) -> CommandAvailability {
    return { .enabled = false, .reason = "no session is running" };
  };
  static_cast<void>(registry.Register(std::move(command)));
  auto invocation
    = registry.BindJson("test.add", json { { "a", 1 }, { "b", 2 } }).value();

  const auto result = registry.Execute(invocation, {});
  const auto description
    = registry.Describe(*registry.Find("test.add"), CommandContext {});

  EXPECT_EQ(result.status, ExecutionStatus::kDisabled);
  EXPECT_EQ(result.error, "no session is running");
  EXPECT_EQ(description["enabled"], false);
  EXPECT_EQ(description["reason"], "no session is running");
}

//! Source policy denies dev-only commands where it says so, and the audit
//! hook sees every outcome.
NOLINT_TEST(CommandRegistry, PolicyDeniesAndAuditRecords)
{
  CommandRegistry registry;
  auto command = AddCommand();
  command.flags = CommandFlags::kDevOnly;
  static_cast<void>(registry.Register(std::move(command)));
  registry.SetSourcePolicy(CommandSource::kRemote,
    CommandRegistry::SourcePolicy { .allow_dev_only = false });
  auto audited = std::vector<CommandRegistry::AuditEvent> {};
  registry.SetAuditHook([&audited](const CommandRegistry::AuditEvent& event) {
    audited.push_back(event);
  });
  const auto invocation
    = registry.BindJson("test.add", json { { "a", 1 }, { "b", 2 } }).value();

  const auto remote = registry.Execute(
    invocation, CommandContext { .source = CommandSource::kRemote });
  const auto local = registry.Execute(invocation, CommandContext {});

  EXPECT_EQ(remote.status, ExecutionStatus::kDenied);
  EXPECT_EQ(local.status, ExecutionStatus::kOk);
  ASSERT_EQ(audited.size(), 2U);
  EXPECT_TRUE(audited[0].denied_by_policy);
  EXPECT_EQ(audited[1].source, CommandSource::kLocalConsole);
}

//! Fixture for commands that span several event-loop turns.
class CommandRegistryAsyncTest : public testing::Test {
protected:
  //! `test.wait ms` sleeps on the loop, then reports how long it waited;
  //! `cleaned_up_` records that its frame unwound.
  auto RegisterWait() -> void
  {
    static_cast<void>(registry_.Register(CommandDefinition {
      .name = "test.wait",
      .label = "Wait",
      .help = "Wait on the test loop",
      .params = {
        CommandParam {
          .name = "ms",
          .type = ParamType::kInt,
          .default_value = ParamValue { int64_t { 10 } },
        },
      },
      .async_handler = [this](CommandArgs args,
                         CommandContext) -> Co<CommandResult> {
        const auto guard = oxygen::ScopeGuard([this] noexcept -> void { cleaned_up_ = true; });
        co_await loop_.Sleep(std::chrono::milliseconds(args.GetInt("ms")));
        co_return CommandResult::Success("waited");
      },
    }));
  }

  TestEventLoop loop_;
  CommandRegistry registry_;
  bool cleaned_up_ { false };
};

//! `Run` awaits an asynchronous handler; `Execute` reports it pending.
NOLINT_TEST_F(CommandRegistryAsyncTest, RunAwaitsAsynchronousHandler)
{
  RegisterWait();
  const auto invocation
    = registry_.BindJson("test.wait", json::object()).value();
  auto result = CommandResult {};

  const auto pending = registry_.Execute(invocation, {});
  oxygen::co::Run(
    loop_, [&]() -> Co<> { result = co_await registry_.Run(invocation, {}); });

  EXPECT_EQ(pending.status, ExecutionStatus::kPending);
  EXPECT_EQ(result.status, ExecutionStatus::kOk);
  EXPECT_EQ(result.output, "waited");
  EXPECT_TRUE(cleaned_up_);
}

//! Cancelling the awaiter cancels the handler, which unwinds.
NOLINT_TEST_F(CommandRegistryAsyncTest, CancellingRunCancelsHandler)
{
  RegisterWait();
  const auto invocation
    = registry_.BindJson("test.wait", json { { "ms", 1000 } }).value();
  bool finished = false;

  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await oxygen::co::AnyOf(
      [&]() -> Co<> {
        static_cast<void>(co_await registry_.Run(invocation, {}));
        finished = true;
      },
      loop_.Sleep(5ms));
  });

  EXPECT_FALSE(finished);
  EXPECT_TRUE(cleaned_up_);
}

//! `cmd.list` describes registered commands with their schemas.
NOLINT_TEST(CommandRegistry, ListDescribesCommands)
{
  CommandRegistry registry;
  static_cast<void>(registry.Register(AddCommand()));
  const auto invocation
    = registry.BindJson("cmd.list", json { { "pattern", "test." } }).value();

  const auto result = registry.Execute(invocation, {});

  ASSERT_EQ(result.status, ExecutionStatus::kOk);
  ASSERT_EQ(result.data.size(), 1U);
  EXPECT_EQ(result.data[0]["name"], "test.add");
  EXPECT_EQ(result.data[0]["params"]["required"], json::array({ "a", "b" }));
}

} // namespace
