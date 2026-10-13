//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/TaskCancelledException.h>

namespace oxygen::commands {

namespace {

  using nlohmann::json;

  constexpr std::string_view kListCommand = "cmd.list";

  auto NotFound(const std::string_view name) -> CommandResult
  {
    return CommandResult::Failure(
      ExecutionStatus::kNotFound, fmt::format("unknown command: {}", name));
  }

  auto InvalidArguments(const std::string_view name, ArgumentErrors errors)
    -> CommandResult
  {
    auto result = CommandResult::Failure(ExecutionStatus::kInvalidArguments,
      fmt::format("{}: {}", name, fmt::join(errors.messages, "; ")));
    result.data = json { { "errors", std::move(errors.messages) } };
    return result;
  }

  auto Matches(const CommandDefinition& command, const std::string_view pattern)
    -> bool
  {
    return pattern.empty() || command.name.find(pattern) != std::string::npos
      || command.label.find(pattern) != std::string::npos;
  }

} // namespace

CommandRegistry::CommandRegistry() { RegisterBuiltins(); }

auto CommandRegistry::Register(CommandDefinition definition) -> CommandHandle
{
  const bool has_one_handler = static_cast<bool>(definition.handler)
    != static_cast<bool>(definition.async_handler);
  if (definition.name.empty() || !has_one_handler
    || commands_.contains(definition.name)) {
    LOG_F(ERROR, "command '{}' not registered: {}", definition.name,
      definition.name.empty() ? "it has no name"
        : !has_one_handler    ? "it needs exactly one handler"
                              : "the name is taken");
    return {};
  }
  const auto handle = CommandHandle { next_handle_++ };
  auto name = definition.name;
  commands_.emplace(std::move(name),
    std::make_shared<Entry>(
      Entry { .handle = handle, .definition = std::move(definition) }));
  return handle;
}

auto CommandRegistry::Unregister(const CommandHandle handle) -> bool
{
  for (auto it = commands_.begin(); it != commands_.end(); ++it) {
    if (it->second->handle == handle) {
      commands_.erase(it);
      return true;
    }
  }
  return false;
}

auto CommandRegistry::Find(const std::string_view name) const
  -> observer_ptr<const CommandDefinition>
{
  const auto entry = FindEntry(name);
  return entry ? observer_ptr { &entry->definition } : nullptr;
}

auto CommandRegistry::List() const
  -> std::vector<observer_ptr<const CommandDefinition>>
{
  auto out = std::vector<observer_ptr<const CommandDefinition>> {};
  out.reserve(commands_.size());
  for (const auto& [_, entry] : commands_) {
    out.emplace_back(&entry->definition);
  }
  return out;
}

auto CommandRegistry::BindText(
  const std::string_view name, const std::span<const std::string> tokens) const
  -> Result<CommandInvocation, CommandResult>
{
  const auto entry = FindEntry(name);
  if (!entry) {
    return Err(NotFound(name));
  }
  auto args = commands::BindText(entry->definition.params, tokens);
  if (!args) {
    return Err(InvalidArguments(name, std::move(args).error()));
  }
  return Ok(CommandInvocation {
    .name = std::string(name),
    .args = std::move(args).value(),
  });
}

auto CommandRegistry::BindJson(
  const std::string_view name, const nlohmann::json& arguments) const
  -> Result<CommandInvocation, CommandResult>
{
  const auto entry = FindEntry(name);
  if (!entry) {
    return Err(NotFound(name));
  }
  auto args = commands::BindJson(entry->definition.params, arguments);
  if (!args) {
    return Err(InvalidArguments(name, std::move(args).error()));
  }
  return Ok(CommandInvocation {
    .name = std::string(name),
    .args = std::move(args).value(),
  });
}

auto CommandRegistry::Availability(const CommandDefinition& command,
  const CommandContext& context) const -> CommandAvailability
{
  if (!IsAllowed(command, context)) {
    return { .enabled = false, .reason = "not allowed from this source" };
  }
  if (command.availability) {
    return command.availability(context);
  }
  return {};
}

auto CommandRegistry::Execute(const CommandInvocation& invocation,
  const CommandContext& context) -> CommandResult
{
  const auto entry = FindEntry(invocation.name);
  if (!entry) {
    auto result = NotFound(invocation.name);
    Audit(context, invocation.name, result.status);
    return result;
  }
  const auto& command = entry->definition;
  if (auto refusal = Admit(command, context)) {
    Audit(context, invocation.name, refusal->status);
    return std::move(*refusal);
  }
  if (!command.handler) {
    return CommandResult::Failure(ExecutionStatus::kPending,
      fmt::format(
        "{} runs across frames; run it through the engine", invocation.name));
  }
  auto result = CommandResult {};
  try {
    result = command.handler(invocation.args, context);
  } catch (const std::exception& ex) {
    result = CommandResult::Failure(ExecutionStatus::kError, ex.what());
  }
  Audit(context, invocation.name, result.status);
  return result;
}

auto CommandRegistry::Run(CommandInvocation invocation, CommandContext context)
  -> co::Co<CommandResult>
{
  // Pin the registration: unregistering during the call must not free it.
  const auto entry = FindEntry(invocation.name);
  if (!entry) {
    auto result = NotFound(invocation.name);
    Audit(context, invocation.name, result.status);
    co_return result;
  }
  const auto& command = entry->definition;
  if (auto refusal = Admit(command, context)) {
    Audit(context, invocation.name, refusal->status);
    co_return std::move(*refusal);
  }
  auto result = CommandResult {};
  try {
    if (command.async_handler) {
      result
        = co_await command.async_handler(std::move(invocation.args), context);
    } else {
      result = command.handler(invocation.args, context);
    }
  } catch (const co::TaskCancelledException&) {
    throw;
  } catch (const std::exception& ex) {
    result = CommandResult::Failure(ExecutionStatus::kError, ex.what());
  }
  Audit(context, invocation.name, result.status);
  co_return result;
}

auto CommandRegistry::Describe(const CommandDefinition& command,
  const CommandContext& context) const -> nlohmann::json
{
  const auto availability = Availability(command, context);
  auto description = json {
    { "name", command.name },
    { "label", command.label },
    { "help", command.help },
    { "flags", to_string(command.flags) },
    { "async", static_cast<bool>(command.async_handler) },
    { "params", ParamsSchema(command.params) },
    { "enabled", availability.enabled },
  };
  if (!availability.enabled) {
    description["reason"] = availability.reason;
  }
  if (command.result_schema) {
    description["result"] = *command.result_schema;
  }
  return description;
}

auto CommandRegistry::SetSourcePolicy(
  const CommandSource source, const SourcePolicy& policy) -> void
{
  policies_[source] = policy;
}

auto CommandRegistry::SetAuditHook(AuditHook hook) -> void
{
  audit_hook_ = std::move(hook);
}

auto CommandRegistry::FindEntry(const std::string_view name) const
  -> std::shared_ptr<Entry>
{
  const auto it = commands_.find(name);
  return it == commands_.end() ? nullptr : it->second;
}

auto CommandRegistry::IsAllowed(
  const CommandDefinition& command, const CommandContext& context) const -> bool
{
  const auto it = policies_.find(context.source);
  const auto policy = it == policies_.end() ? SourcePolicy {} : it->second;
  if (!policy.allow_commands) {
    return false;
  }
  const bool dev_only = HasFlag(command.flags, CommandFlags::kDevOnly);
  const bool cheat = HasFlag(command.flags, CommandFlags::kCheat);
  if (context.shipping_build && (dev_only || cheat)) {
    return false;
  }
  return (!dev_only || policy.allow_dev_only) && (!cheat || policy.allow_cheat);
}

auto CommandRegistry::Admit(const CommandDefinition& command,
  const CommandContext& context) const -> std::optional<CommandResult>
{
  if (!IsAllowed(command, context)) {
    return CommandResult::Failure(ExecutionStatus::kDenied,
      fmt::format(
        "{} is not allowed from {}", command.name, to_string(context.source)));
  }
  if (command.availability) {
    if (auto availability = command.availability(context);
      !availability.enabled) {
      return CommandResult::Failure(
        ExecutionStatus::kDisabled, std::move(availability.reason));
    }
  }
  return std::nullopt;
}

auto CommandRegistry::Audit(const CommandContext& context,
  const std::string_view name, const ExecutionStatus status) const -> void
{
  if (!audit_hook_) {
    return;
  }
  audit_hook_(AuditEvent {
    .source = context.source,
    .client = context.client,
    .name = std::string(name),
    .status = status,
    .denied_by_policy = status == ExecutionStatus::kDenied,
  });
}

auto CommandRegistry::RegisterBuiltins() -> void
{
  static_cast<void>(Register(CommandDefinition {
    .name = std::string(kListCommand),
    .label = "List Commands",
    .help = "List commands whose name or label contains the pattern, with "
            "their parameters, result schema and availability",
    .params = {
      CommandParam {
        .name = "pattern",
        .type = ParamType::kString,
        .help = "Text the name or label contains; empty lists every command",
        .default_value = ParamValue { std::string {} },
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext& context) -> CommandResult {
      const auto& pattern = args.GetString("pattern");
      auto described = json::array();
      auto lines = std::vector<std::string> {};
      for (const auto& command : List()) {
        if (!Matches(*command, pattern)) {
          continue;
        }
        described.push_back(Describe(*command, context));
        lines.push_back(fmt::format("{} - {}", command->name, command->help));
      }
      return CommandResult::Success(
        fmt::format("{}", fmt::join(lines, "\n")), std::move(described));
    },
  }));
}

} // namespace oxygen::commands
