//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Console/Parser.h>
#include <Oxygen/Console/Registry.h>

namespace oxygen::console {

namespace {
  using nlohmann::json;

  auto TrimWhitespace(const std::string_view value) -> std::string_view
  {
    size_t begin = 0;
    while (begin < value.size()
      && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
      ++begin;
    }

    size_t end = value.size();
    while (end > begin
      && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
      --end;
    }

    return value.substr(begin, end - begin);
  }

  auto ToLower(std::string value) -> std::string
  {
    std::ranges::transform(value, value.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
  }

  struct ContainsMatch {
    std::string_view text;
    std::string_view needle;
  };

  auto ContainsCaseInsensitive(const ContainsMatch match) -> bool
  {
    if (match.needle.empty()) {
      return true;
    }
    const auto lowered_text = ToLower(std::string(match.text));
    const auto lowered_needle = ToLower(std::string(match.needle));
    return lowered_text.find(lowered_needle) != std::string::npos;
  }

  auto IsHidden(const CVarDefinition& definition) -> bool
  {
    return HasFlag(definition.flags, CVarFlags::kHidden);
  }

  struct CommandLineAssign final {
    std::string_view name;
    std::string_view value;
  };

  auto ParseCommandLineAssign(
    const std::string_view token, CommandLineAssign& out_assign) -> bool
  {
    const auto split = token.find(kCommandLineAssignSeparator);
    if (split == std::string_view::npos || split == 0
      || split + 1 >= token.size()) {
      return false;
    }
    out_assign.name = token.substr(0, split);
    out_assign.value = token.substr(split + 1);
    return true;
  }

  auto CVarTypeOf(const CVarValue& value) -> CVarType
  {
    if (std::holds_alternative<bool>(value)) {
      return CVarType::kBool;
    }
    if (std::holds_alternative<int64_t>(value)) {
      return CVarType::kInt;
    }
    if (std::holds_alternative<double>(value)) {
      return CVarType::kFloat;
    }
    return CVarType::kString;
  }

  auto SerializeCVarValue(const CVarValue& value) -> json
  {
    if (const auto* boolean = std::get_if<bool>(&value); boolean != nullptr) {
      return *boolean;
    }
    if (const auto* integer = std::get_if<int64_t>(&value);
      integer != nullptr) {
      return *integer;
    }
    if (const auto* floating = std::get_if<double>(&value);
      floating != nullptr) {
      return *floating;
    }
    return std::get<std::string>(value);
  }

  auto DeserializeCVarValue(
    const CVarType type, const json& value, CVarValue& out) -> bool
  {
    switch (type) {
    case CVarType::kBool:
      if (!value.is_boolean()) {
        return false;
      }
      out = value.get<bool>();
      return true;
    case CVarType::kInt:
      if (!value.is_number_integer()) {
        return false;
      }
      out = value.get<int64_t>();
      return true;
    case CVarType::kFloat:
      if (!value.is_number()) {
        return false;
      }
      out = value.get<double>();
      return true;
    case CVarType::kString:
      if (!value.is_string()) {
        return false;
      }
      out = value.get<std::string>();
      return true;
    }
    return false;
  }

} // namespace

Registry::Registry(const size_t history_capacity)
  : history_(history_capacity)
{
  RegisterBuiltinCommands();
}

auto Registry::RegisterCVar(CVarDefinition definition) -> CVarHandle
{
  auto entry = CVarEntry {
    .snapshot = CVarSnapshot {
      .definition = std::move(definition),
      .current_value = {},
      .latched_value = std::nullopt,
      .restart_value = std::nullopt,
    },
    .id = next_id_++,
  };

  entry.snapshot.current_value = entry.snapshot.definition.default_value;
  ClampValue(entry.snapshot.definition, entry.snapshot.current_value);

  const auto [it, inserted]
    = cvars_.emplace(entry.snapshot.definition.name, std::move(entry));
  if (!inserted) {
    return CVarHandle {};
  }
  return CVarHandle { it->second.id };
}

auto Registry::RegisterCommand(CommandDefinition definition) -> CommandHandle
{
  if (!definition.handler || definition.name.empty()) {
    return {};
  }

  const auto id = next_id_++;
  const auto [_, inserted]
    = commands_.emplace(definition.name, std::move(definition));
  if (!inserted) {
    return {};
  }
  return CommandHandle { .id = id };
}

auto Registry::FindCVar(const std::string_view name) const
  -> observer_ptr<const CVarSnapshot>
{
  if (const auto it = cvars_.find(std::string(name)); it != cvars_.end()) {
    return make_observer(&it->second.snapshot);
  }
  return observer_ptr<const CVarSnapshot> {};
}

auto Registry::FindCommand(const std::string_view name) const
  -> observer_ptr<const CommandDefinition>
{
  if (const auto it = commands_.find(std::string(name));
    it != commands_.end()) {
    return make_observer(&it->second);
  }
  return observer_ptr<const CommandDefinition> {};
}

auto Registry::SetCVarFromText(const SetCVarRequest& request,
  const CommandContext& context) -> ExecutionResult
{
  if (const auto it = cvars_.find(std::string(request.name));
    it != cvars_.end()) {
    auto& cvar = it->second.snapshot;
    if (HasFlag(cvar.definition.flags, CVarFlags::kReadOnly)) {
      return {
        .status = ExecutionStatus::kDenied,
        .exit_code = kExitCodeDenied,
        .output = {},
        .error = "cvar is read-only",
      };
    }
    if (!IsCVarMutationAllowed(cvar.definition, context)) {
      return {
        .status = ExecutionStatus::kDenied,
        .exit_code = kExitCodeDenied,
        .output = {},
        .error = "cvar denied by policy",
      };
    }

    CVarValue parsed;
    if (!TryParseValue(cvar.current_value, request.text, parsed)) {
      return {
        .status = ExecutionStatus::kInvalidArguments,
        .exit_code = kExitCodeInvalidArguments,
        .output = {},
        .error = "value parse failed",
      };
    }
    ClampValue(cvar.definition, parsed);

    if (HasFlag(cvar.definition.flags, CVarFlags::kLatched)) {
      cvar.latched_value = std::move(parsed);
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "latched " + cvar.definition.name + " = "
          + ValueToString(*cvar.latched_value),
        .error = {},
      };
    }

    if (HasFlag(cvar.definition.flags, CVarFlags::kRequiresRestart)) {
      cvar.restart_value = std::move(parsed);
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "restart required for " + cvar.definition.name + " = "
          + ValueToString(*cvar.restart_value),
        .error = {},
      };
    }

    cvar.current_value = std::move(parsed);
    return {
      .status = ExecutionStatus::kOk,
      .exit_code = 0,
      .output
      = cvar.definition.name + " = " + ValueToString(cvar.current_value),
      .error = {},
    };
  }

  return {
    .status = ExecutionStatus::kNotFound,
    .exit_code = kExitCodeNotFound,
    .output = {},
    .error = "cvar not found",
  };
}

auto Registry::ApplyLatchedCVars() -> size_t
{
  size_t applied = 0;
  for (auto& [_, cvar] : cvars_) {
    if (cvar.snapshot.latched_value.has_value()) {
      cvar.snapshot.current_value = std::move(*cvar.snapshot.latched_value);
      cvar.snapshot.latched_value.reset();
      ++applied;
    }
  }
  return applied;
}

auto Registry::SaveArchiveCVars(const oxygen::PathFinder& path_finder) const
  -> ExecutionResult
{
  json payload = json::object();
  payload[std::string(kArchiveJsonVersionKey)] = kArchiveJsonVersion1;
  payload[std::string(kArchiveJsonEntriesKey)] = json::array();

  auto& entries = payload[std::string(kArchiveJsonEntriesKey)];
  for (const auto& [name, cvar] : cvars_) {
    if (!HasFlag(cvar.snapshot.definition.flags, CVarFlags::kArchive)) {
      continue;
    }

    entries.push_back(json {
      { std::string(kArchiveJsonNameKey), name },
      { std::string(kArchiveJsonTypeKey),
        std::string(to_string(CVarTypeOf(cvar.snapshot.current_value))) },
      { std::string(kArchiveJsonValueKey),
        SerializeCVarValue(cvar.snapshot.current_value) },
    });
  }

  const auto archive_path = path_finder.CVarsArchivePath();
  const auto archive_dir = archive_path.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(archive_dir, ec);
  if (ec) {
    return {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "unable to create cvar archive directory",
    };
  }

  std::ofstream stream(archive_path);
  if (!stream.is_open()) {
    return {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "unable to open cvar archive file for write",
    };
  }
  stream << payload.dump(kJsonIndentSpaces);
  if (!stream.good()) {
    return {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "failed to write cvar archive file",
    };
  }

  return {
    .status = ExecutionStatus::kOk,
    .exit_code = 0,
    .output = "saved cvar archive to " + archive_path.generic_string(),
    .error = {},
  };
}

auto Registry::LoadArchiveCVars(const oxygen::PathFinder& path_finder,
  const CommandContext& context) -> ExecutionResult
{
  const auto archive_path = path_finder.CVarsArchivePath();
  std::ifstream stream(archive_path);
  if (!stream.is_open()) {
    return {
      .status = ExecutionStatus::kNotFound,
      .exit_code = kExitCodeNotFound,
      .output = {},
      .error = "cvar archive file not found",
    };
  }

  json payload;
  try {
    stream >> payload;
  } catch (...) {
    return {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "cvar archive json parse failed",
    };
  }

  if (!payload.is_object()
    || !payload.contains(std::string(kArchiveJsonEntriesKey))
    || !payload[std::string(kArchiveJsonEntriesKey)].is_array()) {
    return {
      .status = ExecutionStatus::kInvalidArguments,
      .exit_code = kExitCodeInvalidArguments,
      .output = {},
      .error = "invalid cvar archive schema",
    };
  }

  size_t applied = 0;
  const auto& entries = payload[std::string(kArchiveJsonEntriesKey)];
  for (const auto& entry : entries) {
    if (!entry.is_object()) {
      continue;
    }
    if (!entry.contains(std::string(kArchiveJsonNameKey))
      || !entry[std::string(kArchiveJsonNameKey)].is_string()) {
      continue;
    }
    if (!entry.contains(std::string(kArchiveJsonValueKey))) {
      continue;
    }

    const auto name
      = entry[std::string(kArchiveJsonNameKey)].get<std::string>();
    const auto cvar_it = cvars_.find(name);
    if (cvar_it == cvars_.end()) {
      continue;
    }
    auto& snapshot = cvar_it->second.snapshot;
    CVarValue loaded_value;
    if (!DeserializeCVarValue(CVarTypeOf(snapshot.current_value),
          entry[std::string(kArchiveJsonValueKey)], loaded_value)) {
      continue;
    }

    const auto text_value = ValueToString(loaded_value);
    const auto result
      = SetCVarFromText({ .name = name, .text = text_value }, context);
    if (result.status == ExecutionStatus::kOk) {
      ++applied;
    }
  }

  return {
    .status = ExecutionStatus::kOk,
    .exit_code = 0,
    .output = "loaded " + std::to_string(applied) + " cvar override(s) from "
      + archive_path.generic_string(),
    .error = {},
  };
}

auto Registry::ApplyCommandLineOverrides(
  const std::span<const std::string_view> arguments,
  const CommandContext& context) -> ExecutionResult
{
  size_t applied = 0;
  for (size_t i = 0; i < arguments.size(); ++i) {
    std::string_view token = arguments[i];
    if (!token.starts_with(kCommandLineSetPrefix)) {
      continue;
    }

    token = token.substr(kCommandLineSetPrefix.size());
    if (token.empty()) {
      return {
        .status = ExecutionStatus::kInvalidArguments,
        .exit_code = kExitCodeInvalidArguments,
        .output = {},
        .error = "empty command line cvar override",
      };
    }

    auto assign = CommandLineAssign {};
    if (!ParseCommandLineAssign(token, assign)) {
      if (i + 1 >= arguments.size()) {
        return {
          .status = ExecutionStatus::kInvalidArguments,
          .exit_code = kExitCodeInvalidArguments,
          .output = {},
          .error = "command line override missing value",
        };
      }
      assign.name = token;
      assign.value = arguments[++i];
    }

    const auto result
      = SetCVarFromText({ .name = assign.name, .text = assign.value }, context);
    if (result.status != ExecutionStatus::kOk) {
      return result;
    }
    ++applied;
  }

  return {
    .status = ExecutionStatus::kOk,
    .exit_code = 0,
    .output
    = "applied " + std::to_string(applied) + " command line override(s)",
    .error = {},
  };
}

auto Registry::SetSourcePolicy(
  const CommandSource source, const SourcePolicy& policy) -> void
{
  switch (source) {
  case CommandSource::kLocalConsole:
    local_source_policy_ = policy;
    break;
  case CommandSource::kConfigFile:
    config_source_policy_ = policy;
    break;
  case CommandSource::kRemote:
    remote_source_policy_ = policy;
    break;
  case CommandSource::kAutomation:
    automation_source_policy_ = policy;
    break;
  }
}

auto Registry::SetRemoteAllowlist(std::vector<std::string> allowlist) -> void
{
  remote_allowlist_.clear();
  for (auto& name : allowlist) {
    if (!name.empty()) {
      remote_allowlist_.emplace(std::move(name));
    }
  }
}

auto Registry::ClearRemoteAllowlist() -> void { remote_allowlist_.clear(); }

auto Registry::SetAuditHook(AuditHook hook) -> void
{
  audit_hook_ = std::move(hook);
}

auto Registry::Execute(
  const std::string_view line, const CommandContext& context) -> ExecutionResult
{
  const auto text = std::string(line);
  const auto commands = Parser::SplitCommands(text);
  if (commands.empty()) {
    return {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "empty command",
    };
  }

  history_.Push(text);
  ExecutionResult last {
    .status = ExecutionStatus::kOk,
    .exit_code = 0,
    .output = {},
    .error = {},
  };
  for (const auto& command_line : commands) {
    last = ExecuteSingle(command_line, context);
    if (last.status != ExecutionStatus::kOk) {
      return last;
    }
  }
  return last;
}

auto Registry::ExecuteSingle(
  const std::string_view line, const CommandContext& context) -> ExecutionResult
{
  const auto emit
    = [this, line, context](const std::string& subject,
        const ExecutionResult& result, const bool denied_by_policy) {
        EmitAudit(AuditEvent {
          .source = context.source,
          .line = std::string(line),
          .subject = subject,
          .status = result.status,
          .denied_by_policy = denied_by_policy,
        });
      };

  const auto tokens = Parser::Tokenize(line);
  if (tokens.empty()) {
    const auto result = ExecutionResult {
      .status = ExecutionStatus::kError,
      .exit_code = kExitCodeGenericError,
      .output = {},
      .error = "empty command",
    };
    emit({}, result, false);
    return result;
  }

  const auto& first = tokens.front();

  if (const auto cvar_it = cvars_.find(first); cvar_it != cvars_.end()) {
    if (tokens.size() == 1) {
      RecordCompletionUsage(first);
      const auto result = ExecutionResult {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = cvar_it->second.snapshot.definition.name + " = "
          + ValueToString(cvar_it->second.snapshot.current_value),
        .error = {},
      };
      emit(first, result, false);
      return result;
    }

    std::string new_value = tokens[1];
    if (std::holds_alternative<std::string>(
          cvar_it->second.snapshot.current_value)
      && tokens.size() > 2) {
      for (size_t i = 2; i < tokens.size(); ++i) {
        new_value.push_back(' ');
        new_value.append(tokens[i]);
      }
    }
    const auto result
      = SetCVarFromText({ .name = first, .text = new_value }, context);
    if (result.status == ExecutionStatus::kOk) {
      RecordCompletionUsage(first);
    }
    emit(first, result, result.status == ExecutionStatus::kDenied);
    return result;
  }

  if (const auto command_it = commands_.find(first);
    command_it != commands_.end()) {
    const auto& command = command_it->second;
    if (!IsCommandAllowed(command, context)) {
      const auto result = ExecutionResult {
        .status = ExecutionStatus::kDenied,
        .exit_code = kExitCodeDenied,
        .output = {},
        .error = "command denied by policy",
      };
      emit(first, result, true);
      return result;
    }

    auto tail = tokens | std::views::drop(1);
    std::vector<std::string> args(tail.begin(), tail.end());

    const auto result = command.handler(args, context);
    if (result.status == ExecutionStatus::kOk) {
      RecordCompletionUsage(first);
    }
    emit(first, result, result.status == ExecutionStatus::kDenied);
    return result;
  }

  const auto result = ExecutionResult {
    .status = ExecutionStatus::kNotFound,
    .exit_code = kExitCodeNotFound,
    .output = {},
    .error = "unknown command/cvar: " + first,
  };
  emit(first, result, false);
  return result;
}

auto Registry::Complete(const std::string_view prefix) const
  -> std::vector<CompletionCandidate>
{
  std::vector<CompletionCandidate> out;

  for (const auto& [name, command] : commands_) {
    if (StartsWithCaseInsensitive({ .text = name, .prefix = prefix })) {
      out.push_back({
        .kind = CompletionKind::kCommand,
        .token = name,
        .help = command.help,
      });
    }
  }
  for (const auto& [name, cvar] : cvars_) {
    if (IsHidden(cvar.snapshot.definition)) {
      continue;
    }
    if (StartsWithCaseInsensitive({ .text = name, .prefix = prefix })) {
      out.push_back({
        .kind = CompletionKind::kCVar,
        .token = name,
        .help = cvar.snapshot.definition.help,
      });
    }
  }

  std::sort(out.begin(), out.end(), [this](const auto& lhs, const auto& rhs) {
    const auto lhs_usage = completion_usage_.find(lhs.token);
    const auto rhs_usage = completion_usage_.find(rhs.token);

    const auto lhs_frequency
      = lhs_usage != completion_usage_.end() ? lhs_usage->second.frequency : 0;
    const auto rhs_frequency
      = rhs_usage != completion_usage_.end() ? rhs_usage->second.frequency : 0;
    if (lhs_frequency != rhs_frequency) {
      return lhs_frequency > rhs_frequency;
    }

    const auto lhs_tick = lhs_usage != completion_usage_.end()
      ? lhs_usage->second.last_used_tick
      : 0;
    const auto rhs_tick = rhs_usage != completion_usage_.end()
      ? rhs_usage->second.last_used_tick
      : 0;
    if (lhs_tick != rhs_tick) {
      return lhs_tick > rhs_tick;
    }

    if (lhs.token != rhs.token) {
      return lhs.token < rhs.token;
    }
    return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
  });
  return out;
}

auto Registry::BeginCompletionCycle(const std::string_view prefix)
  -> observer_ptr<const CompletionCandidate>
{
  completion_cycle_.Begin(prefix, Complete(prefix));
  return completion_cycle_.Current();
}

auto Registry::NextCompletion() -> observer_ptr<const CompletionCandidate>
{
  return completion_cycle_.Next();
}

auto Registry::PreviousCompletion() -> observer_ptr<const CompletionCandidate>
{
  return completion_cycle_.Previous();
}

auto Registry::CurrentCompletion() const
  -> observer_ptr<const CompletionCandidate>
{
  return completion_cycle_.Current();
}

auto Registry::GetHistory() const -> const History& { return history_; }

void Registry::RegisterBuiltinCommands()
{
  (void)RegisterCommand(CommandDefinition {
    .name = std::string(kBuiltinHelpCommand),
    .help = "help [name] - show command/cvar help",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>& args,
                 const CommandContext&) -> ExecutionResult {
      if (args.empty()) {
        return {
          .status = ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "builtins: help, find, list, exec",
          .error = {},
        };
      }

      const auto& name = args.front();
      if (const auto command = FindCommand(name); command != nullptr) {
        return {
          .status = ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "command " + command->name
            + " flags=" + to_string(command->flags) + " : " + command->help,
          .error = {},
        };
      }

      if (const auto cvar = FindCVar(name); cvar != nullptr) {
        if (IsHidden(cvar->definition)) {
          return {
            .status = ExecutionStatus::kNotFound,
            .exit_code = kExitCodeNotFound,
            .output = {},
            .error = "name not found",
          };
        }
        return {
          .status = ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "cvar " + cvar->definition.name
            + " flags=" + to_string(cvar->definition.flags)
            + " default=" + ValueToString(cvar->definition.default_value)
            + " current=" + ValueToString(cvar->current_value) + " : "
            + cvar->definition.help,
          .error = {},
        };
      }

      return {
        .status = ExecutionStatus::kNotFound,
        .exit_code = kExitCodeNotFound,
        .output = {},
        .error = "name not found",
      };
    },
  });

  (void)RegisterCommand(CommandDefinition {
    .name = std::string(kBuiltinFindCommand),
    .help = "find <pattern> - search commands and cvars",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>& args,
                 const CommandContext&) -> ExecutionResult {
      if (args.empty()) {
        return {
          .status = ExecutionStatus::kInvalidArguments,
          .exit_code = kExitCodeInvalidArguments,
          .output = {},
          .error = "find requires a search pattern",
        };
      }

      std::string pattern = args.front();
      for (size_t i = 1; i < args.size(); ++i) {
        pattern += " ";
        pattern += args[i];
      }

      std::vector<std::string> matches;
      for (const auto& [name, command] : commands_) {
        if (ContainsCaseInsensitive({ .text = name, .needle = pattern })
          || ContainsCaseInsensitive(
            { .text = command.help, .needle = pattern })) {
          matches.push_back("cmd  " + name + " - " + command.help);
        }
      }
      for (const auto& [name, cvar] : cvars_) {
        if (IsHidden(cvar.snapshot.definition)) {
          continue;
        }
        if (ContainsCaseInsensitive({ .text = name, .needle = pattern })
          || ContainsCaseInsensitive(
            { .text = cvar.snapshot.definition.help, .needle = pattern })) {
          matches.push_back(
            "cvar " + name + " - " + cvar.snapshot.definition.help);
        }
      }

      if (matches.empty()) {
        return {
          .status = ExecutionStatus::kNotFound,
          .exit_code = kExitCodeNotFound,
          .output = {},
          .error = "no matches",
        };
      }

      std::sort(matches.begin(), matches.end());
      std::string output;
      for (size_t i = 0; i < matches.size(); ++i) {
        output += matches[i];
        if (i + 1 < matches.size()) {
          output += "\n";
        }
      }

      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = std::move(output),
        .error = {},
      };
    },
  });

  (void)RegisterCommand(CommandDefinition {
    .name = std::string(kBuiltinListCommand),
    .help = "list [all|commands|cvars] - list registered console symbols",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>& args,
                 const CommandContext&) -> ExecutionResult {
      std::string_view mode = kListModeAll;
      if (!args.empty()) {
        mode = args.front();
      }

      const bool include_commands
        = mode == kListModeAll || mode == kListModeCommands;
      const bool include_cvars = mode == kListModeAll || mode == kListModeCVars;
      if (!include_commands && !include_cvars) {
        return {
          .status = ExecutionStatus::kInvalidArguments,
          .exit_code = kExitCodeInvalidArguments,
          .output = {},
          .error = "list mode must be all|commands|cvars",
        };
      }

      std::vector<std::string> lines;
      if (include_commands) {
        for (const auto& [name, command] : commands_) {
          lines.push_back("cmd  " + name + " - " + command.help);
        }
      }
      if (include_cvars) {
        for (const auto& [name, cvar] : cvars_) {
          if (IsHidden(cvar.snapshot.definition)) {
            continue;
          }
          lines.push_back(
            "cvar " + name + " - " + cvar.snapshot.definition.help);
        }
      }

      std::sort(lines.begin(), lines.end());
      std::string output;
      for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        if (i + 1 < lines.size()) {
          output += "\n";
        }
      }

      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = std::move(output),
        .error = {},
      };
    },
  });

  (void)RegisterCommand(CommandDefinition {
    .name = std::string(kBuiltinExecCommand),
    .help = "exec <path> - execute script file line by line",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>& args,
                 const CommandContext& context) -> ExecutionResult {
      if (args.empty()) {
        return {
          .status = ExecutionStatus::kInvalidArguments,
          .exit_code = kExitCodeInvalidArguments,
          .output = {},
          .error = "exec requires a file path",
        };
      }

      std::string path = args.front();
      for (size_t i = 1; i < args.size(); ++i) {
        path += " ";
        path += args[i];
      }
      return ExecuteScriptFile(path, context);
    },
  });
}

auto Registry::ExecuteScriptFile(
  const std::string_view path, const CommandContext& context) -> ExecutionResult
{
  if (script_depth_ >= kMaxScriptDepth) {
    return {
      .status = ExecutionStatus::kDenied,
      .exit_code = kExitCodeDenied,
      .output = {},
      .error = "max script depth exceeded",
    };
  }

  std::ifstream script { std::string(path) };
  if (!script.is_open()) {
    return {
      .status = ExecutionStatus::kNotFound,
      .exit_code = kExitCodeNotFound,
      .output = {},
      .error = "script file not found",
    };
  }

  struct ScriptDepthGuard final {
    uint32_t& depth;
    explicit ScriptDepthGuard(uint32_t& in_depth)
      : depth(in_depth)
    {
    }
    ~ScriptDepthGuard() { --depth; }
    OXYGEN_MAKE_NON_COPYABLE(ScriptDepthGuard)
    OXYGEN_MAKE_NON_MOVABLE(ScriptDepthGuard)
  };
  ++script_depth_;
  const auto guard = ScriptDepthGuard(script_depth_);

  constexpr size_t kLineStart = 1;
  size_t line_number = kLineStart;
  std::string line;
  ExecutionResult last {
    .status = ExecutionStatus::kOk,
    .exit_code = 0,
    .output = {},
    .error = {},
  };

  while (std::getline(script, line)) {
    const auto trimmed = TrimWhitespace(line);
    if (trimmed.empty() || trimmed.starts_with(kScriptCommentPrefixHash)
      || trimmed.starts_with(kScriptCommentPrefixDoubleSlash)) {
      ++line_number;
      continue;
    }

    last = Execute(trimmed,
      CommandContext {
        .source = CommandSource::kConfigFile,
        .shipping_build = context.shipping_build,
      });
    if (last.status != ExecutionStatus::kOk) {
      last.error = "script " + std::string(path) + ":"
        + std::to_string(line_number) + ": " + last.error;
      return last;
    }
    ++line_number;
  }

  return last;
}

void Registry::RecordCompletionUsage(const std::string_view token)
{
  auto& usage = completion_usage_[std::string(token)];
  ++usage.frequency;
  usage.last_used_tick = completion_tick_++;
}

auto Registry::TryParseValue(const CVarValue& hint, const std::string_view text,
  CVarValue& out_value) -> bool
{
  if (std::holds_alternative<bool>(hint)) {
    if (text == "1" || text == "true" || text == "on") {
      out_value = true;
      return true;
    }
    if (text == "0" || text == "false" || text == "off") {
      out_value = false;
      return true;
    }
    return false;
  }

  if (std::holds_alternative<int64_t>(hint)) {
    std::stringstream stream;
    stream << text;
    int64_t value { 0 };
    stream >> value;
    if (!stream.fail() && stream.eof()) {
      out_value = value;
      return true;
    }
    return false;
  }

  if (std::holds_alternative<double>(hint)) {
    std::stringstream stream;
    stream << text;
    double value { 0.0 };
    stream >> value;
    if (!stream.fail() && stream.eof()) {
      out_value = value;
      return true;
    }
    return false;
  }

  out_value = std::string(text);
  return true;
}

auto Registry::ClampValue(const CVarDefinition& definition, CVarValue& value)
  -> void
{
  const auto clamp_double = [&](double& v) {
    if (definition.min_value.has_value()) {
      v = std::max(v, *definition.min_value);
    }
    if (definition.max_value.has_value()) {
      v = std::min(v, *definition.max_value);
    }
  };

  if (auto* integer = std::get_if<int64_t>(&value)) {
    auto numeric = static_cast<double>(*integer);
    clamp_double(numeric);
    *integer = static_cast<int64_t>(std::llround(numeric));
  } else if (auto* floating = std::get_if<double>(&value)) {
    clamp_double(*floating);
  }
}

auto Registry::ValueToString(const CVarValue& value) -> std::string
{
  if (const auto* v = std::get_if<bool>(&value)) {
    return *v ? "true" : "false";
  }
  if (const auto* v = std::get_if<int64_t>(&value)) {
    return std::to_string(*v);
  }
  if (const auto* v = std::get_if<double>(&value)) {
    return std::to_string(*v);
  }
  return std::get<std::string>(value);
}

auto Registry::SourcePolicyFor(const CommandSource source) const
  -> const SourcePolicy&
{
  switch (source) {
  case CommandSource::kLocalConsole:
    return local_source_policy_;
  case CommandSource::kConfigFile:
    return config_source_policy_;
  case CommandSource::kRemote:
    return remote_source_policy_;
  case CommandSource::kAutomation:
    return automation_source_policy_;
  }
  return local_source_policy_;
}

auto Registry::IsCommandAllowed(
  const CommandDefinition& command, const CommandContext& context) const -> bool
{
  const auto& policy = SourcePolicyFor(context.source);
  if (!policy.allow_commands) {
    return false;
  }

  if (context.shipping_build) {
    if (HasFlag(command.flags, CommandFlags::kCheat)
      || HasFlag(command.flags, CommandFlags::kDevOnly)) {
      return false;
    }
  }

  if (!policy.allow_dev_only
    && HasFlag(command.flags, CommandFlags::kDevOnly)) {
    return false;
  }
  if (!policy.allow_cheat && HasFlag(command.flags, CommandFlags::kCheat)) {
    return false;
  }

  if (context.source == CommandSource::kRemote
    && !HasFlag(command.flags, CommandFlags::kRemoteAllowed)) {
    return false;
  }

  if (context.source == CommandSource::kRemote && !remote_allowlist_.empty()
    && !remote_allowlist_.contains(command.name)) {
    return false;
  }

  return true;
}

auto Registry::IsCVarMutationAllowed(
  const CVarDefinition& definition, const CommandContext& context) const -> bool
{
  const auto& policy = SourcePolicyFor(context.source);
  if (!policy.allow_cvars) {
    return false;
  }

  if (context.shipping_build) {
    if (HasFlag(definition.flags, CVarFlags::kCheat)
      || HasFlag(definition.flags, CVarFlags::kDevOnly)) {
      return false;
    }
  }

  if (!policy.allow_dev_only
    && HasFlag(definition.flags, CVarFlags::kDevOnly)) {
    return false;
  }
  if (!policy.allow_cheat && HasFlag(definition.flags, CVarFlags::kCheat)) {
    return false;
  }
  return true;
}

auto Registry::EmitAudit(const AuditEvent& event) const -> void
{
  if (audit_hook_) {
    audit_hook_(event);
  }
}

} // namespace oxygen::console
