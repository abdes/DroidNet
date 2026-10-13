//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/History.h>
#include <Oxygen/Console/StartupPlan.h>

namespace oxygen::console {

Console::Console(const observer_ptr<commands::CommandRegistry> commands,
  const size_t history_capacity)
  : registry_(commands, history_capacity)
{
}

auto Console::RegisterCVar(CVarDefinition definition,
  const CVarRegistrationOptions& options) -> CVarHandle
{
  return registry_.RegisterCVar(std::move(definition), options);
}

auto Console::SetCVarFromText(const Registry::SetCVarRequest& request,
  const commands::CommandContext& context) -> commands::CommandResult
{
  return registry_.SetCVarFromText(request, context);
}

auto Console::ApplyStartupPlan(const ConsoleStartupPlan& startup_plan) -> void
{
  registry_.ApplyStartupPlan(startup_plan);
}

auto Console::Execute(const std::string_view line,
  const commands::CommandContext& context) -> commands::CommandResult
{
  return registry_.Execute(line, context);
}

auto Console::Parse(const std::string_view line) const
  -> Result<std::vector<commands::CommandInvocation>, commands::CommandResult>
{
  return registry_.Parse(line);
}

auto Console::RecordHistory(const std::string_view line) -> void
{
  registry_.RecordHistory(line);
}

auto Console::RecordExecution(std::string line, commands::CommandResult result)
  -> void
{
  registry_.RecordExecution(std::move(line), std::move(result));
}

auto Console::RecordUsage(const commands::CommandInvocation& invocation) -> void
{
  registry_.RecordUsage(invocation);
}

auto Console::Complete(const std::string_view prefix) const
  -> std::vector<CompletionCandidate>
{
  return registry_.Complete(prefix);
}

auto Console::BeginCompletionCycle(const std::string_view prefix)
  -> observer_ptr<const CompletionCandidate>
{
  return registry_.BeginCompletionCycle(prefix);
}

auto Console::NextCompletion() -> observer_ptr<const CompletionCandidate>
{
  return registry_.NextCompletion();
}

auto Console::PreviousCompletion() -> observer_ptr<const CompletionCandidate>
{
  return registry_.PreviousCompletion();
}

auto Console::CurrentCompletion() const
  -> observer_ptr<const CompletionCandidate>
{
  return registry_.CurrentCompletion();
}

auto Console::ApplyLatchedCVars() -> size_t
{
  return registry_.ApplyLatchedCVars();
}

auto Console::SaveArchiveCVars(const oxygen::PathFinder& path_finder,
  const ArchiveSaveMode mode) -> commands::CommandResult
{
  return registry_.SaveArchiveCVars(path_finder, mode);
}

auto Console::LoadArchiveCVars(const oxygen::PathFinder& path_finder,
  const commands::CommandContext& context) -> commands::CommandResult
{
  return registry_.LoadArchiveCVars(path_finder, context);
}

auto Console::ApplyCommandLineOverrides(
  std::span<const std::string_view> arguments,
  const commands::CommandContext& context) -> commands::CommandResult
{
  return registry_.ApplyCommandLineOverrides(arguments, context);
}

auto Console::SaveHistory(const oxygen::PathFinder& path_finder) const
  -> commands::CommandResult
{
  return registry_.SaveHistory(path_finder);
}

auto Console::LoadHistory(const oxygen::PathFinder& path_finder)
  -> commands::CommandResult
{
  return registry_.LoadHistory(path_finder);
}

auto Console::ListSymbols(const bool include_hidden) const
  -> std::vector<ConsoleSymbol>
{
  return registry_.ListSymbols(include_hidden);
}

auto Console::SetSourcePolicy(const commands::CommandSource source,
  const Registry::SourcePolicy& policy) -> void
{
  registry_.SetSourcePolicy(source, policy);
}

auto Console::GetHistory() const -> const History&
{
  return registry_.GetHistory();
}

auto Console::GetExecutionRecords() const
  -> const std::vector<Registry::ExecutionRecord>&
{
  return registry_.GetExecutionRecords();
}

auto Console::ClearExecutionRecords() -> void
{
  registry_.ClearExecutionRecords();
}

auto Console::FindCVar(const std::string_view name) const
  -> observer_ptr<const CVarSnapshot>
{
  return registry_.FindCVar(name);
}

auto Console::Commands() const noexcept
  -> observer_ptr<commands::CommandRegistry>
{
  return registry_.Commands();
}

} // namespace oxygen::console
