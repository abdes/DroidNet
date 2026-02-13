//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <Oxygen/Console/Console.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using namespace oxygen::console;

  Console console {};

  const auto cvar_ok = console.RegisterCVar(CVarDefinition {
    .name = "r.vsync",
    .help = "Enable vsync",
    .default_value = int64_t { 1 },
    .flags = CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 1.0,
  });

  const auto command_ok = console.RegisterCommand(CommandDefinition {
    .name = "echo",
    .help = "Echo args",
    .flags = CommandFlags::kNone,
    .handler = [](const std::vector<std::string>& args,
                 const CommandContext&) -> ExecutionResult {
      std::string text;
      for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
          text.push_back(' ');
        }
        text += args[i];
      }
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = text,
        .error = {},
      };
    },
  });

  if (!cvar_ok.IsValid() || !command_ok.IsValid()) {
    return EXIT_FAILURE;
  }

  const auto set_result = console.Execute("r.vsync 0");
  if (set_result.status != ExecutionStatus::kOk) {
    return EXIT_FAILURE;
  }

  const auto command_result = console.Execute("echo hello oxygen");
  if (command_result.status != ExecutionStatus::kOk
    || command_result.output != "hello oxygen") {
    return EXIT_FAILURE;
  }

  const auto completions = console.Complete("r.");
  if (completions.empty()) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
