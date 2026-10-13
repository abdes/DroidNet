//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstdlib>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  namespace c = oxygen::console;

  oxygen::commands::CommandRegistry command_registry;
  c::Console console { oxygen::observer_ptr { &command_registry } };

  const auto cvar_ok = console.RegisterCVar(c::CVarDefinition {
    .name = "r.vsync",
    .help = "Enable vsync",
    .default_value = int64_t { 1 },
    .flags = c::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 1.0,
  });

  const auto command_ok
    = command_registry.Register(oxygen::commands::CommandDefinition {
      .name = "echo",
      .label = "Echo",
      .help = "Echo text",
      .params = {
        oxygen::commands::CommandParam {
          .name = "text",
          .type = oxygen::commands::ParamType::kString,
          .required = true,
          .rest_of_line = true,
        },
      },
      .handler = [](const oxygen::commands::CommandArgs& args,
                   const oxygen::commands::CommandContext&)
        -> oxygen::commands::CommandResult {
        return oxygen::commands::CommandResult::Success(
          args.GetString("text"));
      },
    });

  if (!cvar_ok.IsValid() || !command_ok.IsValid()) {
    return EXIT_FAILURE;
  }

  const auto set_result = console.Execute("r.vsync 0");
  if (set_result.status != oxygen::commands::ExecutionStatus::kOk) {
    return EXIT_FAILURE;
  }

  const auto command_result = console.Execute("echo hello oxygen");
  if (command_result.status != oxygen::commands::ExecutionStatus::kOk
    || command_result.output != "hello oxygen") {
    return EXIT_FAILURE;
  }

  const auto completions = console.Complete("r.");
  if (completions.empty()) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
