//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::commands::CommandContext;
using oxygen::commands::CommandDefinition;
using oxygen::commands::CommandFlags;
using oxygen::commands::CommandResult;
using oxygen::commands::CommandSource;
using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::CVarValueOrigin;

NOLINT_TEST(ConsolePolicy, EnforcesShippingBuildRestrictions)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };

  // DevOnly Command
  ASSERT_TRUE(command_registry
      .Register({
        .name = "sys.dev_cmd",
        .help = "Dev cmd",
        .flags = CommandFlags::kDevOnly,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  // Regular Command
  ASSERT_TRUE(command_registry
      .Register({
        .name = "sys.normal_cmd",
        .help = "Normal cmd",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
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

NOLINT_TEST(ConsolePolicy, AppliesSourcePolicyMatrix)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
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
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
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
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kAppDefault);
  ASSERT_TRUE(snapshot->restart_value.has_value());
  EXPECT_EQ(std::get<std::string>(*snapshot->restart_value), "vulkan");
}

} // namespace
