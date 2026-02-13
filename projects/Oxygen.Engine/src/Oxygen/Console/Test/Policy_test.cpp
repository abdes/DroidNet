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
using oxygen::console::CommandSource;
using oxygen::console::Console;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionResult;
using oxygen::console::ExecutionStatus;
using oxygen::console::Registry;

NOLINT_TEST(ConsolePolicy, EnforcesShippingBuildRestrictions)
{
  Console console {};

  // DevOnly Command
  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "sys.dev_cmd",
        .help = "Dev cmd",
        .flags = CommandFlags::kDevOnly,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  // Regular Command
  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "sys.normal_cmd",
        .help = "Normal cmd",
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

  // DevOnly allowed in non-shipping
  EXPECT_EQ(
    console
      .Execute("sys.dev_cmd",
        { .source = CommandSource::kLocalConsole, .shipping_build = false })
      .status,
    ExecutionStatus::kOk);
  // DevOnly denied in shipping
  EXPECT_EQ(
    console
      .Execute("sys.dev_cmd",
        { .source = CommandSource::kLocalConsole, .shipping_build = true })
      .status,
    ExecutionStatus::kDenied);
  // Normal allowed in both
  EXPECT_EQ(
    console
      .Execute("sys.normal_cmd",
        { .source = CommandSource::kLocalConsole, .shipping_build = false })
      .status,
    ExecutionStatus::kOk);
  EXPECT_EQ(
    console
      .Execute("sys.normal_cmd",
        { .source = CommandSource::kLocalConsole, .shipping_build = true })
      .status,
    ExecutionStatus::kOk);
}

NOLINT_TEST(ConsolePolicy, EnforcesRemoteAllowlist)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "net.ping",
        .help = "Ping",
        .flags = CommandFlags::kRemoteAllowed,
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
      .RegisterCommand({
        .name = "net.dangerous",
        .help = "Dangerous",
        .flags = CommandFlags::kRemoteAllowed,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  console.SetRemoteAllowlist({ "net.ping" });

  // Allowed because it is in the allowlist
  EXPECT_EQ(console
              .Execute("net.ping",
                { .source = CommandSource::kRemote, .shipping_build = false })
              .status,
    ExecutionStatus::kOk);
  // Blocked because it is not in the allowlist even though it has the flag
  EXPECT_EQ(console
              .Execute("net.dangerous",
                { .source = CommandSource::kRemote, .shipping_build = false })
              .status,
    ExecutionStatus::kDenied);
  // Local still allowed
  EXPECT_EQ(
    console
      .Execute("net.dangerous",
        { .source = CommandSource::kLocalConsole, .shipping_build = false })
      .status,
    ExecutionStatus::kOk);
}

NOLINT_TEST(ConsolePolicy, EmitsAuditHooks)
{
  Console console {};
  std::vector<Registry::AuditEvent> events;
  console.SetAuditHook(
    [&events](const Registry::AuditEvent& event) { events.push_back(event); });

  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.back().subject, "help");
}

NOLINT_TEST(ConsolePolicy, AppliesSourcePolicyMatrix)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.val",
        .help = "Val",
        .default_value = int64_t { 0 },
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  // Disable CVars for Automation
  console.SetSourcePolicy(CommandSource::kAutomation,
    {
      .allow_commands = true,
      .allow_cvars = false,
      .allow_dev_only = true,
      .allow_cheat = true,
    });

  EXPECT_EQ(
    console
      .Execute("sys.val 1",
        { .source = CommandSource::kAutomation, .shipping_build = false })
      .status,
    ExecutionStatus::kDenied);
  EXPECT_EQ(
    console
      .Execute("sys.val 1",
        { .source = CommandSource::kLocalConsole, .shipping_build = false })
      .status,
    ExecutionStatus::kOk);
}

NOLINT_TEST(ConsolePolicy, SupportsRequiresRestart)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "gfx.backend",
        .help = "Backend",
        .default_value = std::string("d3d12"),
        .flags = CVarFlags::kRequiresRestart,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  const auto set_result = console.Execute("gfx.backend vulkan");
  EXPECT_EQ(set_result.status, ExecutionStatus::kOk);

  const auto snapshot = console.FindCVar("gfx.backend");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<std::string>(snapshot->current_value), "d3d12");
  ASSERT_TRUE(snapshot->restart_value.has_value());
  EXPECT_EQ(std::get<std::string>(*snapshot->restart_value), "vulkan");
}

} // namespace
