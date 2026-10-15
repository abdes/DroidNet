//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "LightBench/LightBenchConsoleBindings.h"
#include "LightBench/LightBenchSettings.h"

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>

namespace oxygen::examples::light_bench {
namespace {
  auto Invalid(const char* message) -> commands::CommandResult
  {
    return {
      .status = commands::ExecutionStatus::kInvalidArguments,
      .error = message,
    };
  }
} // namespace

LightBenchConsoleBindings::LightBenchConsoleBindings(
  console::Console& console, Actions actions)
  : console_(console)
  , actions_(std::move(actions))
{
  if (!actions_.select || !actions_.reset || !actions_.inspect) {
    throw std::invalid_argument("LightBench console actions must be present");
  }
  bool registered = false;
  const auto rollback = ScopeGuard([&] noexcept -> void {
    if (!registered) {
      Unregister();
    }
  });
  auto& registry = *console_.Commands();
  handles_.at(0) = registry.Register({
    .name = "lightbench.presets",
    .label = "LightBench Presets",
    .help = "List the available complete LightBench presets.",
    .handler = [](const commands::CommandArgs&,
                 const commands::CommandContext&) -> commands::CommandResult {
      std::string output;
      for (const auto& preset : GetPresets()) {
        output += std::string(preset.id) + " - " + preset.name + "\n";
      }
      return { .output = std::move(output) };
    },
  });
  handles_.at(1) = registry.Register({
    .name = "lightbench.preset",
    .label = "LightBench Preset",
    .help = "Inspect the active and pending configuration, or queue a "
            "complete preset for the next frame.",
    .params = {
      commands::CommandParam {
        .name = "id",
        .type = commands::ParamType::kString,
        .help = "Preset id (see lightbench.presets); empty inspects",
        .default_value = commands::ParamValue { std::string {} },
      },
    },
    .handler = [this](const commands::CommandArgs& args,
                 const commands::CommandContext&) -> commands::CommandResult {
      const auto& id = args.GetString("id");
      if (id.empty()) {
        return commands::CommandResult::Success(actions_.inspect());
      }
      for (const auto& preset : GetPresets()) {
        if (preset.id == id) {
          actions_.select(preset.preset);
          return {
            .output = "queued preset=" + std::string(preset.id)
              + "; applies at the next frame boundary",
          };
        }
      }
      return Invalid("Unknown preset id; use lightbench.presets.");
    },
  });
  handles_.at(2) = registry.Register({
    .name = "lightbench.reset",
    .label = "Reset LightBench Preset",
    .help
    = "Queue reset of the active LightBench preset at the next frame boundary.",
    .handler = [this](const commands::CommandArgs&,
                 const commands::CommandContext&) -> commands::CommandResult {
      actions_.reset();
      return {
        .output
        = "queued reset of active preset; applies at the next frame boundary",
      };
    },
  });
  if (std::ranges::any_of(handles_,
        [](const auto& handle) -> bool { return !handle.IsValid(); })) {
    throw std::runtime_error("Cannot register LightBench console commands");
  }
  registered = true;
}

LightBenchConsoleBindings::~LightBenchConsoleBindings() { Unregister(); }
auto LightBenchConsoleBindings::Unregister() -> void
{
  for (const auto handle : handles_) {
    console_.Commands()->Unregister(handle);
  }
  handles_.fill(commands::CommandHandle {});
}

} // namespace oxygen::examples::light_bench
