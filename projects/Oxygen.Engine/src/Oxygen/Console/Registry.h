//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/History.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen {
class PathFinder;
}

namespace oxygen::console {

//! Console variables, and the console's text front end over commands.
/*!
 The registry owns console variables (registration, latching, persistence,
 startup values) and turns console lines into command invocations. Commands
 themselves live in a `commands::CommandRegistry`; this registry adds the
 console built-ins (`help`, `find`, `list`, `exec`, `cvar.*`) to it and
 removes them again when destroyed.
*/
class Registry final {
public:
  struct SetCVarRequest {
    std::string_view name;
    std::string_view text;
  };

  //! Which sources may change console variables.
  struct SourcePolicy {
    bool allow_cvars { true };
    bool allow_dev_only { true };
    bool allow_cheat { true };
  };

  struct ExecutionRecord {
    std::string line;
    commands::CommandResult result;
  };

  OXGN_CONS_API explicit Registry(
    observer_ptr<commands::CommandRegistry> commands,
    size_t history_capacity = kDefaultHistoryCapacity);
  OXGN_CONS_API ~Registry();

  OXYGEN_MAKE_NON_COPYABLE(Registry)
  OXYGEN_MAKE_NON_MOVABLE(Registry)

  OXGN_CONS_NDAPI auto RegisterCVar(CVarDefinition definition,
    const CVarRegistrationOptions& options = {}) -> CVarHandle;
  OXGN_CONS_NDAPI auto FindCVar(std::string_view name) const
    -> observer_ptr<const CVarSnapshot>;
  OXGN_CONS_NDAPI auto Commands() const noexcept
    -> observer_ptr<commands::CommandRegistry>
  {
    return commands_;
  }

  OXGN_CONS_NDAPI auto SetCVarFromText(const SetCVarRequest& request,
    const commands::CommandContext& context = {}) -> commands::CommandResult;
  OXGN_CONS_API auto ApplyStartupPlan(const ConsoleStartupPlan& startup_plan)
    -> void;
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

  OXGN_CONS_API auto SetSourcePolicy(
    commands::CommandSource source, const SourcePolicy& policy) -> void;

  //! Parse a console line (commands separated by `;`) into invocations; a
  //! variable name alone reads it (`cvar.get`), followed by a value sets it
  //! (`cvar.set`).
  OXGN_CONS_NDAPI auto Parse(std::string_view line) const
    -> Result<std::vector<commands::CommandInvocation>,
      commands::CommandResult>;
  //! Run a console line synchronously; an asynchronous command in it returns
  //! `kPending` (a command dispatcher runs those).
  OXGN_CONS_NDAPI auto Execute(std::string_view line,
    const commands::CommandContext& context = {}) -> commands::CommandResult;
  //! Record a line typed in the console, for history and completion ranking.
  OXGN_CONS_API auto RecordHistory(std::string_view line) -> void;
  //! Record the outcome of a line, for console output.
  OXGN_CONS_API auto RecordExecution(
    std::string line, commands::CommandResult result) -> void;
  //! Record a command of a line that ran successfully, for completion
  //! ranking.
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

  OXGN_CONS_NDAPI auto GetHistory() const -> const History&;
  OXGN_CONS_NDAPI auto GetExecutionRecords() const
    -> const std::vector<ExecutionRecord>&;
  OXGN_CONS_API auto ClearExecutionRecords() -> void;

private:
  struct CompletionUsage {
    uint64_t frequency { 0 };
    uint64_t last_used_tick { 0 };
  };

  struct CVarEntry {
    CVarSnapshot snapshot;
    uint32_t id { 0 };
    std::optional<CVarValueOrigin> latched_origin {};
    std::optional<CVarValueOrigin> restart_origin {};
    std::optional<CVarValue> persisted_value {};
  };

  enum class ApplyValueDisposition : uint8_t {
    kSilent,
    kReport,
  };

  struct ApplyValueResult {
    commands::CommandResult result;
    bool applied { false };
  };

  void RegisterBuiltinCommands();
  OXGN_CONS_NDAPI auto ExecuteScriptFile(std::string_view path,
    const commands::CommandContext& context) -> commands::CommandResult;
  OXGN_CONS_NDAPI auto IsCVarMutationAllowed(const CVarDefinition& definition,
    const commands::CommandContext& context) const -> bool;
  OXGN_CONS_NDAPI auto SourcePolicyFor(commands::CommandSource source) const
    -> const SourcePolicy&;

  [[nodiscard]] static auto TryParseValue(
    const CVarValue& hint, std::string_view text, CVarValue& out_value) -> bool;
  static auto ClampValue(const CVarDefinition& definition, CVarValue& value)
    -> void;
  [[nodiscard]] static auto ValueToString(const CVarValue& value)
    -> std::string;
  [[nodiscard]] static auto OriginRank(CVarValueOrigin origin) noexcept -> int;
  [[nodiscard]] auto ApplyStampedValue(CVarEntry& cvar,
    const StampedCVarValue& stamped_value, ApplyValueDisposition disposition)
    -> ApplyValueResult;
  auto ApplyCachedStartupValue(CVarEntry& cvar) -> bool;
  auto ApplyCachedPersistedValue(CVarEntry& cvar) -> bool;
  void RecordCompletionUsage(std::string_view token);

  History history_;
  uint32_t next_id_ { 1 };
  uint64_t completion_tick_ { 1 };
  uint32_t script_depth_ { 0 };
  CompletionCycle completion_cycle_;
  std::unordered_map<std::string, CompletionUsage> completion_usage_;
  observer_ptr<commands::CommandRegistry> commands_;
  std::vector<commands::CommandHandle> builtin_handles_;

  SourcePolicy local_source_policy_ {};
  SourcePolicy config_source_policy_ {
    .allow_cvars = true,
    .allow_dev_only = true,
    .allow_cheat = false,
  };
  SourcePolicy remote_source_policy_ {};
  SourcePolicy automation_source_policy_ {
    .allow_cvars = true,
    .allow_dev_only = true,
    .allow_cheat = false,
  };

  std::unordered_map<std::string, CVarEntry> cvars_;
  std::unordered_map<std::string, StampedCVarValue> startup_cvar_values_;
  std::unordered_map<std::string, CVarValue> persisted_cvar_values_;
  size_t execution_record_capacity_ { kDefaultExecutionRecordCapacity };
  std::vector<ExecutionRecord> execution_records_;
};

} // namespace oxygen::console
