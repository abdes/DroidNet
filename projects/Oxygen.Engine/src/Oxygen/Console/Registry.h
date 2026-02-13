//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/History.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen {
class PathFinder;
}

namespace oxygen::console {

class Registry final {
public:
  struct SetCVarRequest {
    std::string_view name;
    std::string_view text;
  };

  struct SourcePolicy {
    bool allow_commands { true };
    bool allow_cvars { true };
    bool allow_dev_only { true };
    bool allow_cheat { true };
  };

  struct AuditEvent {
    CommandSource source { CommandSource::kLocalConsole };
    std::string line;
    std::string subject;
    ExecutionStatus status { ExecutionStatus::kOk };
    bool denied_by_policy { false };
  };

  struct ExecutionRecord {
    std::string line;
    ExecutionResult result;
  };

  using AuditHook = std::function<void(const AuditEvent&)>;

  OXGN_CONS_API explicit Registry(
    size_t history_capacity = kDefaultHistoryCapacity);
  ~Registry() = default;

  OXYGEN_MAKE_NON_COPYABLE(Registry)
  OXYGEN_MAKE_NON_MOVABLE(Registry)

  OXGN_CONS_NDAPI auto RegisterCVar(CVarDefinition definition) -> CVarHandle;
  OXGN_CONS_NDAPI auto RegisterCommand(CommandDefinition definition)
    -> CommandHandle;

  OXGN_CONS_NDAPI auto FindCVar(std::string_view name) const
    -> observer_ptr<const CVarSnapshot>;
  OXGN_CONS_NDAPI auto FindCommand(std::string_view name) const
    -> observer_ptr<const CommandDefinition>;

  OXGN_CONS_NDAPI auto SetCVarFromText(const SetCVarRequest& request,
    const CommandContext& context = {}) -> ExecutionResult;
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
  OXGN_CONS_NDAPI auto SaveHistory(
    const oxygen::PathFinder& path_finder) const -> ExecutionResult;
  OXGN_CONS_NDAPI auto LoadHistory(
    const oxygen::PathFinder& path_finder) -> ExecutionResult;
  OXGN_CONS_NDAPI auto ListSymbols(bool include_hidden = false) const
    -> std::vector<ConsoleSymbol>;

  OXGN_CONS_API auto SetSourcePolicy(
    CommandSource source, const SourcePolicy& policy) -> void;
  OXGN_CONS_API auto SetRemoteAllowlist(std::vector<std::string> allowlist)
    -> void;
  OXGN_CONS_API auto ClearRemoteAllowlist() -> void;
  OXGN_CONS_API auto SetAuditHook(AuditHook hook) -> void;

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
  };

  void RegisterBuiltinCommands();
  OXGN_CONS_NDAPI auto ExecuteSingle(
    std::string_view line, const CommandContext& context) -> ExecutionResult;
  OXGN_CONS_NDAPI auto ExecuteScriptFile(
    std::string_view path, const CommandContext& context) -> ExecutionResult;
  OXGN_CONS_NDAPI auto IsCommandAllowed(const CommandDefinition& command,
    const CommandContext& context) const -> bool;
  OXGN_CONS_NDAPI auto IsCVarMutationAllowed(const CVarDefinition& definition,
    const CommandContext& context) const -> bool;
  OXGN_CONS_NDAPI auto SourcePolicyFor(CommandSource source) const
    -> const SourcePolicy&;
  OXGN_CONS_API auto EmitAudit(const AuditEvent& event) const -> void;

  [[nodiscard]] static auto TryParseValue(
    const CVarValue& hint, std::string_view text, CVarValue& out_value) -> bool;
  static auto ClampValue(const CVarDefinition& definition, CVarValue& value)
    -> void;
  [[nodiscard]] static auto ValueToString(const CVarValue& value)
    -> std::string;
  void RecordCompletionUsage(std::string_view token);

  History history_;
  uint32_t next_id_ { 1 };
  uint64_t completion_tick_ { 1 };
  uint32_t script_depth_ { 0 };
  CompletionCycle completion_cycle_;
  std::unordered_map<std::string, CompletionUsage> completion_usage_;
  std::unordered_set<std::string> remote_allowlist_;
  AuditHook audit_hook_;

  SourcePolicy local_source_policy_ {};
  SourcePolicy config_source_policy_ {
    .allow_commands = true,
    .allow_cvars = true,
    .allow_dev_only = true,
    .allow_cheat = false,
  };
  SourcePolicy remote_source_policy_ {
    .allow_commands = true,
    .allow_cvars = false,
    .allow_dev_only = false,
    .allow_cheat = false,
  };
  SourcePolicy automation_source_policy_ {
    .allow_commands = true,
    .allow_cvars = true,
    .allow_dev_only = true,
    .allow_cheat = false,
  };

  std::unordered_map<std::string, CVarEntry> cvars_;
  std::unordered_map<std::string, CommandDefinition> commands_;
  size_t execution_record_capacity_ { kDefaultExecutionRecordCapacity };
  std::vector<ExecutionRecord> execution_records_;
};

} // namespace oxygen::console
