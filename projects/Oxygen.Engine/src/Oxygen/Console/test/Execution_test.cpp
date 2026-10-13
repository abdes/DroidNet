//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <fstream>
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
using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;
using oxygen::console::CVarFlags;

NOLINT_TEST(ConsoleExecution, SupportsCommandChaining)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  bool was_called = false;

  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.val",
        .help = "System value",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  ASSERT_TRUE(command_registry
      .Register({
        .name = "sys.mark",
        .help = "Mark command",
        .flags = CommandFlags::kNone,
        .handler = [&was_called](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          was_called = true;
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  const auto result = console.Execute("sys.val 4; sys.mark");
  EXPECT_EQ(result.status, ExecutionStatus::kOk);
  EXPECT_TRUE(was_called);
  EXPECT_EQ(std::get<int64_t>(console.FindCVar("sys.val")->current_value), 4);
}

NOLINT_TEST(ConsoleExecution, SupportsScriptExecution)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "r.exposure",
        .help = "Exposure",
        .default_value = 1.0,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  const auto script_path
    = std::filesystem::temp_directory_path() / "oxygen_console_script.cfg";
  {
    std::ofstream script { script_path };
    script << "# comment\n";
    script << "// double slash comment\n";
    script << "r.exposure 2.5\n";
  }

  const auto result
    = console.Execute("exec \"" + script_path.generic_string() + "\"");
  EXPECT_EQ(result.status, ExecutionStatus::kOk);

  const auto snapshot = console.FindCVar("r.exposure");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_DOUBLE_EQ(std::get<double>(snapshot->current_value), 2.5);

  std::filesystem::remove(script_path);
}

NOLINT_TEST(ConsoleExecution, HandlesExecutionErrorsInChains)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  int call_count = 0;
  ASSERT_TRUE(command_registry
      .Register({
        .name = "cmd.inc",
        .help = "Increment",
        .flags = CommandFlags::kNone,
        .handler = [&call_count](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          call_count++;
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  ASSERT_TRUE(command_registry
      .Register({
        .name = "cmd.fail",
        .help = "Fails",
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult::Failure(ExecutionStatus::kError, "failed");
        },
      })
      .IsValid());

  // The whole line is checked before anything runs: a typo runs nothing.
  const auto unknown = console.Execute("cmd.inc; unknown.cmd; cmd.inc");
  EXPECT_EQ(unknown.status, ExecutionStatus::kNotFound);
  EXPECT_EQ(call_count, 0);

  // A command that fails while running stops the rest of the line.
  const auto failed = console.Execute("cmd.inc; cmd.fail; cmd.inc");
  EXPECT_EQ(failed.status, ExecutionStatus::kError);
  EXPECT_EQ(call_count, 1);
}

NOLINT_TEST(ConsoleExecution, ProvidesBuiltinHelpFindAndList)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };

  ASSERT_TRUE(command_registry
      .Register({
        .name = "sys.custom",
        .help = "Custom command help",
        .flags = CommandFlags::kNone,
        .handler = [](const oxygen::commands::CommandArgs&,
                     const CommandContext&) -> CommandResult {
          return CommandResult {
            .status = ExecutionStatus::kOk, .output = {}, .error = {}
          };
        },
      })
      .IsValid());

  const auto list_result = console.Execute("list commands");
  EXPECT_EQ(list_result.status, ExecutionStatus::kOk);
  EXPECT_NE(list_result.output.find("sys.custom"), std::string::npos);

  const auto help_result = console.Execute("help sys.custom");
  EXPECT_EQ(help_result.status, ExecutionStatus::kOk);
  EXPECT_NE(help_result.output.find("Custom command help"), std::string::npos);

  const auto find_result = console.Execute("find custom");
  EXPECT_EQ(find_result.status, ExecutionStatus::kOk);
  EXPECT_NE(find_result.output.find("sys.custom"), std::string::npos);
}

} // namespace
