//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Console/Console.h>

namespace oxygen::console {

Console::Console(const size_t history_capacity)
  : registry_(history_capacity)
{
}

auto Console::RegisterCVar(CVarDefinition definition) -> CVarHandle
{
  return registry_.RegisterCVar(std::move(definition));
}

auto Console::RegisterCommand(CommandDefinition definition) -> CommandHandle
{
  return registry_.RegisterCommand(std::move(definition));
}

auto Console::Execute(
  const std::string_view line, const CommandContext& context) -> ExecutionResult
{
  return registry_.Execute(line, context);
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

auto Console::SaveArchiveCVars(const oxygen::PathFinder& path_finder) const
  -> ExecutionResult
{
  return registry_.SaveArchiveCVars(path_finder);
}

auto Console::LoadArchiveCVars(const oxygen::PathFinder& path_finder,
  const CommandContext& context) -> ExecutionResult
{
  return registry_.LoadArchiveCVars(path_finder, context);
}

auto Console::ApplyCommandLineOverrides(
  std::span<const std::string_view> arguments, const CommandContext& context)
  -> ExecutionResult
{
  return registry_.ApplyCommandLineOverrides(arguments, context);
}

auto Console::SetSourcePolicy(
  const CommandSource source, const Registry::SourcePolicy& policy) -> void
{
  registry_.SetSourcePolicy(source, policy);
}

auto Console::SetRemoteAllowlist(std::vector<std::string> allowlist) -> void
{
  registry_.SetRemoteAllowlist(std::move(allowlist));
}

auto Console::ClearRemoteAllowlist() -> void
{
  registry_.ClearRemoteAllowlist();
}

auto Console::SetAuditHook(Registry::AuditHook hook) -> void
{
  registry_.SetAuditHook(std::move(hook));
}

auto Console::GetHistory() const -> const History&
{
  return registry_.GetHistory();
}

auto Console::FindCVar(const std::string_view name) const
  -> observer_ptr<const CVarSnapshot>
{
  return registry_.FindCVar(name);
}

} // namespace oxygen::console
