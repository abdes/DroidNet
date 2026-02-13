//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Console.h>

namespace {

using oxygen::console::CommandContext;
using oxygen::console::CommandDefinition;
using oxygen::console::CommandFlags;
using oxygen::console::Console;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionResult;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsoleExecution, SupportsCommandChaining)
{
  Console console {};
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

  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "sys.mark",
        .help = "Mark command",
        .flags = CommandFlags::kNone,
        .handler = [&was_called](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          was_called = true;
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
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
  Console console {};
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
  Console console {};
  int call_count = 0;
  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "cmd.inc",
        .help = "Increment",
        .flags = CommandFlags::kNone,
        .handler = [&call_count](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          call_count++;
          return ExecutionResult { .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = {},
            .error = {} };
        },
      })
      .IsValid());

  // Chain should stop at the first error
  const auto result = console.Execute("cmd.inc; unknown.cmd; cmd.inc");
  EXPECT_EQ(result.status, ExecutionStatus::kNotFound);
  EXPECT_EQ(call_count, 1);
}

NOLINT_TEST(ConsoleExecution, ProvidesBuiltinHelpFindAndList)
{
  Console console {};

  ASSERT_TRUE(console
      .RegisterCommand({
        .name = "sys.custom",
        .help = "Custom command help",
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
