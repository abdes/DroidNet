//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/Registry.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen {
class PathFinder;
}

namespace oxygen::console {

class Console final {
public:
  OXGN_CONS_API explicit Console(
    observer_ptr<commands::CommandRegistry> commands,
    size_t history_capacity = kDefaultHistoryCapacity);
  ~Console() = default;

  OXYGEN_MAKE_NON_COPYABLE(Console)
  OXYGEN_MAKE_NON_MOVABLE(Console)

  OXGN_CONS_NDAPI auto RegisterCVar(CVarDefinition definition,
    const CVarRegistrationOptions& options = {}) -> CVarHandle;
  OXGN_CONS_NDAPI auto SetCVarFromText(const Registry::SetCVarRequest& request,
    const commands::CommandContext& context = {}) -> commands::CommandResult;
  OXGN_CONS_API auto ApplyStartupPlan(const ConsoleStartupPlan& startup_plan)
    -> void;
  //! Run a console line synchronously; an asynchronous command in it returns
  //! `kPending`.
  OXGN_CONS_NDAPI auto Execute(std::string_view line,
    const commands::CommandContext& context = {}) -> commands::CommandResult;
  //! A console line as invocations, for a command dispatcher.
  OXGN_CONS_NDAPI auto Parse(std::string_view line) const
    -> Result<std::vector<commands::CommandInvocation>,
      commands::CommandResult>;
  OXGN_CONS_API auto RecordHistory(std::string_view line) -> void;
  OXGN_CONS_API auto RecordExecution(
    std::string line, commands::CommandResult result) -> void;
  OXGN_CONS_API auto RecordUsage(const commands::CommandInvocation& invocation)
    -> void;
  OXGN_CONS_NDAPI auto Complete(std::string_view prefix) const
    -> std::vector<CompletionCandidate>;
  OXGN_CONS_NDAPI auto BeginCompletionCycle(std::string_view prefix)
    -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto NextCompletion()
    -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto PreviousCompletion()
    -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto CurrentCompletion() const
    -> observer_ptr<const CompletionCandidate>;
  OXGN_CONS_NDAPI auto ApplyLatchedCVars() -> size_t;
  OXGN_CONS_NDAPI auto SaveArchiveCVars(const oxygen::PathFinder& path_finder,
    ArchiveSaveMode mode = ArchiveSaveMode::kAutomatic)
    -> commands::CommandResult;
  OXGN_CONS_NDAPI auto LoadArchiveCVars(const oxygen::PathFinder& path_finder,
    const commands::CommandContext& context = commands::CommandContext {
      .source = commands::CommandSource::kConfigFile,
      .shipping_build = false,
    }) -> commands::CommandResult;
  OXGN_CONS_NDAPI auto ApplyCommandLineOverrides(
    std::span<const std::string_view> arguments,
    const commands::CommandContext& context = commands::CommandContext {
      .source = commands::CommandSource::kAutomation,
      .shipping_build = false,
    }) -> commands::CommandResult;
  OXGN_CONS_NDAPI auto SaveHistory(const oxygen::PathFinder& path_finder) const
    -> commands::CommandResult;
  OXGN_CONS_NDAPI auto LoadHistory(const oxygen::PathFinder& path_finder)
    -> commands::CommandResult;
  OXGN_CONS_NDAPI auto ListSymbols(bool include_hidden = false) const
    -> std::vector<ConsoleSymbol>;

  OXGN_CONS_API auto SetSourcePolicy(commands::CommandSource source,
    const Registry::SourcePolicy& policy) -> void;

  OXGN_CONS_NDAPI auto GetHistory() const -> const History&;
  OXGN_CONS_NDAPI auto GetExecutionRecords() const
    -> const std::vector<Registry::ExecutionRecord>&;
  OXGN_CONS_API auto ClearExecutionRecords() -> void;
  OXGN_CONS_NDAPI auto FindCVar(std::string_view name) const
    -> observer_ptr<const CVarSnapshot>;
  //! The command registry this console runs lines through.
  OXGN_CONS_NDAPI auto Commands() const noexcept
    -> observer_ptr<commands::CommandRegistry>;

  template <typename TValue>
  [[nodiscard]] auto TryGetCVarValue(
    std::string_view name, TValue& out_value) const noexcept -> bool
  {
    const auto snapshot = FindCVar(name);
    if (snapshot == nullptr) {
      return false;
    }
    const auto* value = std::get_if<TValue>(&snapshot->current_value);
    if (value == nullptr) {
      return false;
    }
    out_value = *value;
    return true;
  }

private:
  Registry registry_;
};

} // namespace oxygen::console
