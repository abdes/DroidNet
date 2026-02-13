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
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/Registry.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen {
class PathFinder;
}

namespace oxygen::console {

class Console final {
public:
  OXGN_CONS_API explicit Console(
    size_t history_capacity = kDefaultHistoryCapacity);
  ~Console() = default;

  OXYGEN_MAKE_NON_COPYABLE(Console)
  OXYGEN_MAKE_NON_MOVABLE(Console)

  OXGN_CONS_NDAPI auto RegisterCVar(CVarDefinition definition) -> CVarHandle;
  OXGN_CONS_NDAPI auto RegisterCommand(CommandDefinition definition)
    -> CommandHandle;
  OXGN_CONS_NDAPI auto Execute(std::string_view line,
    const CommandContext& context = {}) -> ExecutionResult;
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
  OXGN_CONS_NDAPI auto SaveArchiveCVars(
    const oxygen::PathFinder& path_finder) const -> ExecutionResult;
  OXGN_CONS_NDAPI auto LoadArchiveCVars(const oxygen::PathFinder& path_finder,
    const CommandContext& context = CommandContext {
      .source = CommandSource::kConfigFile,
      .shipping_build = false,
    }) -> ExecutionResult;
  OXGN_CONS_NDAPI auto ApplyCommandLineOverrides(
    std::span<const std::string_view> arguments,
    const CommandContext& context = CommandContext {
      .source = CommandSource::kAutomation,
      .shipping_build = false,
    }) -> ExecutionResult;

  OXGN_CONS_API auto SetSourcePolicy(
    CommandSource source, const Registry::SourcePolicy& policy) -> void;
  OXGN_CONS_API auto SetRemoteAllowlist(std::vector<std::string> allowlist)
    -> void;
  OXGN_CONS_API auto ClearRemoteAllowlist() -> void;
  OXGN_CONS_API auto SetAuditHook(Registry::AuditHook hook) -> void;

  OXGN_CONS_NDAPI auto GetHistory() const -> const History&;
  OXGN_CONS_NDAPI auto FindCVar(std::string_view name) const
    -> observer_ptr<const CVarSnapshot>;

private:
  Registry registry_;
};

} // namespace oxygen::console
