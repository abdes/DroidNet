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
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/Constants.h>

namespace oxygen::examples::light_bench {
namespace {
  auto Invalid(const char* message) -> console::ExecutionResult
  {
    return {
      .status = console::ExecutionStatus::kInvalidArguments,
      .exit_code = console::kExitCodeInvalidArguments,
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
  handles_.at(0) = console_.RegisterCommand({
    .name = "lightbench.presets",
    .help = "List the available complete LightBench presets.",
    .flags = console::CommandFlags::kNone,
    .handler = [](const auto& args, const auto&) -> console::ExecutionResult {
      if (!args.empty()) {
        return Invalid("lightbench.presets takes no arguments.");
      }
      std::string output;
      for (const auto& preset : GetPresets()) {
        output += std::string(preset.id) + " - " + preset.name + "\n";
      }
      return { .output = std::move(output) };
    },
  });
  handles_.at(1) = console_.RegisterCommand({
    .name = "lightbench.preset",
    .help = "lightbench.preset [id] — inspect active/pending configuration, or "
            "queue a complete preset for the next frame.",
    .flags = console::CommandFlags::kNone,
    .handler
    = [this](const auto& args, const auto&) -> console::ExecutionResult {
      if (args.empty()) {
        return { .output = actions_.inspect() };
      }
      if (args.size() != 1) {
        return Invalid("Expected one preset id; use lightbench.presets.");
      }
      for (const auto& preset : GetPresets()) {
        if (preset.id == args.front()) {
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
  handles_.at(2) = console_.RegisterCommand({
    .name = "lightbench.reset",
    .help
    = "Queue reset of the active LightBench preset at the next frame boundary.",
    .flags = console::CommandFlags::kNone,
    .handler
    = [this](const auto& args, const auto&) -> console::ExecutionResult {
      if (!args.empty()) {
        return Invalid("lightbench.reset takes no arguments.");
      }
      actions_.reset();
      return {
        .output
        = "queued reset of active preset; applies at the next frame boundary",
      };
    },
  });
  if (std::ranges::any_of(handles_,
        [](const auto handle) -> auto { return !handle.IsValid(); })) {
    throw std::runtime_error("Cannot register LightBench console commands");
  }
  registered = true;
}

LightBenchConsoleBindings::~LightBenchConsoleBindings() { Unregister(); }
auto LightBenchConsoleBindings::Unregister() -> void
{
  for (const auto handle : handles_) {
    console_.UnregisterCommand(handle);
  }
  handles_.fill(console::CommandHandle {});
}

} // namespace oxygen::examples::light_bench
