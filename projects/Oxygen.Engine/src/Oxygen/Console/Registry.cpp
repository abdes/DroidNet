//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Completion.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Console/History.h>
#include <Oxygen/Console/Parser.h>
#include <Oxygen/Console/Registry.h>
#include <Oxygen/Console/StartupPlan.h>

namespace oxygen::console {

using commands::CommandArgs;
using commands::CommandContext;
using commands::CommandDefinition;
using commands::CommandInvocation;
using commands::CommandParam;
using commands::CommandResult;
using commands::CommandSource;
using commands::ExecutionStatus;
using commands::ParamType;
using commands::ParamValue;

namespace {
  using nlohmann::json;

  constexpr std::string_view kCVarGetCommand = "cvar.get";
  constexpr std::string_view kCVarSetCommand = "cvar.set";
  constexpr std::string_view kCVarListCommand = "cvar.list";

  auto Failure(const ExecutionStatus status, std::string error) -> CommandResult
  {
    return CommandResult::Failure(status, std::move(error));
  }

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
    std::ranges::transform(
      value, value.begin(), [](const unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
      });
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
    return lowered_text.contains(lowered_needle);
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

  auto TryParseCVarTypeName(const std::string_view text, CVarType& out_type)
    -> bool
  {
    const auto lowered = ToLower(std::string(text));
    if (lowered == "bool") {
      out_type = CVarType::kBool;
      return true;
    }
    if (lowered == "int") {
      out_type = CVarType::kInt;
      return true;
    }
    if (lowered == "float") {
      out_type = CVarType::kFloat;
      return true;
    }
    if (lowered == "string") {
      out_type = CVarType::kString;
      return true;
    }
    return false;
  }

  auto IsAllowedValue(const CVarDefinition& definition, const CVarValue& value)
    -> bool
  {
    const auto* text = std::get_if<std::string>(&value);
    return definition.allowed_values.empty() || text == nullptr
      || std::ranges::contains(definition.allowed_values, *text);
  }

  auto IsCompatibleValueType(
    const CVarDefinition& definition, const CVarValue& value) -> bool
  {
    return CVarTypeOf(definition.default_value) == CVarTypeOf(value)
      && IsAllowedValue(definition, value);
  }

  auto JoinAllowedValues(const CVarDefinition& definition) -> std::string
  {
    std::string joined;
    for (const auto& allowed : definition.allowed_values) {
      if (!joined.empty()) {
        joined += ", ";
      }
      joined += allowed;
    }
    return joined;
  }

  auto LogIncompatibleStampedValue(const std::string_view source,
    const CVarDefinition& definition, const CVarValue& value) -> void
  {
    if (CVarTypeOf(definition.default_value) == CVarTypeOf(value)) {
      LOG_F(WARNING,
        "Console {} value for '{}' ignored: '{}' is not one of: {}", source,
        definition.name, std::get<std::string>(value),
        JoinAllowedValues(definition));
      return;
    }
    LOG_F(WARNING,
      "Console {} value for '{}' ignored: expected type {} but got {}", source,
      definition.name, to_string(CVarTypeOf(definition.default_value)),
      to_string(CVarTypeOf(value)));
  }

  auto NotAllowedResult(const CVarDefinition& definition,
    const std::string_view text) -> CommandResult
  {
    return {
      .status = ExecutionStatus::kInvalidArguments,
      .output = {},
      .error = "value '" + std::string(text)
        + "' is not one of: " + JoinAllowedValues(definition),
    };
  }

  auto HistoryPathFor(const oxygen::PathFinder& path_finder)
    -> std::filesystem::path
  {
    return path_finder.CVarsArchivePath().parent_path() / kHistoryFileName;
  }

  //! A `kScalar` argument as the text a console variable parses.
  auto ScalarText(const ParamValue& value) -> std::string
  {
    if (const auto* text = std::get_if<std::string>(&value)) {
      return *text;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
      return *boolean ? "true" : "false";
    }
    if (const auto* integer = std::get_if<int64_t>(&value)) {
      return std::to_string(*integer);
    }
    return fmt::format("{}", std::get<double>(value));
  }

  auto DescribeCVar(const CVarSnapshot& cvar) -> json
  {
    auto described = json {
      { "name", cvar.definition.name },
      { "type", to_string(CVarTypeOf(cvar.definition.default_value)) },
      { "value", SerializeCVarValue(cvar.current_value) },
      { "default", SerializeCVarValue(cvar.definition.default_value) },
      { "origin", to_string(cvar.current_origin) },
      { "flags", to_string(cvar.definition.flags) },
      { "help", cvar.definition.help },
    };
    if (cvar.definition.min_value) {
      described["minimum"] = *cvar.definition.min_value;
    }
    if (cvar.definition.max_value) {
      described["maximum"] = *cvar.definition.max_value;
    }
    if (!cvar.definition.allowed_values.empty()) {
      described["allowed_values"] = cvar.definition.allowed_values;
    }
    if (cvar.latched_value) {
      described["latched"] = SerializeCVarValue(*cvar.latched_value);
    }
    if (cvar.restart_value) {
      described["restart"] = SerializeCVarValue(*cvar.restart_value);
    }
    return described;
  }

  //! The symbol whose use an invocation counts toward completion ranking.
  auto CompletionToken(const CommandInvocation& invocation) -> std::string_view
  {
    if ((invocation.name == kCVarGetCommand
          || invocation.name == kCVarSetCommand)
      && invocation.args.Has("name")) {
      return invocation.args.GetString("name");
    }
    return invocation.name;
  }

} // namespace

Registry::Registry(const observer_ptr<commands::CommandRegistry> commands,
  const size_t history_capacity)
  : history_(history_capacity)
  , commands_(commands)
{
  CHECK_NOTNULL_F(commands_, "the console needs a command registry");
  RegisterBuiltinCommands();
}

Registry::~Registry()
{
  for (const auto handle : builtin_handles_) {
    commands_->Unregister(handle);
  }
}

auto Registry::RegisterCVar(CVarDefinition definition,
  const CVarRegistrationOptions& options) -> CVarHandle
{
  if (next_id_ == 0U) {
    return {};
  }
  auto entry = CVarEntry {
    .snapshot = CVarSnapshot {
      .definition = std::move(definition),
      .current_value = {},
      .current_origin = CVarValueOrigin::kAppDefault,
      .latched_value = std::nullopt,
      .restart_value = std::nullopt,
    },
    .id = next_id_++,
  };

  DCHECK_F(IsAllowedValue(entry.snapshot.definition,
             entry.snapshot.definition.default_value),
    "cvar '{}' default is not one of its allowed values",
    entry.snapshot.definition.name);
  entry.snapshot.current_value = entry.snapshot.definition.default_value;
  ClampValue(entry.snapshot.definition, entry.snapshot.current_value);
  if (options.initial.has_value()
    && IsCompatibleValueType(
      entry.snapshot.definition, options.initial->value)) {
    auto initial = *options.initial;
    ClampValue(entry.snapshot.definition, initial.value);
    (void)ApplyStampedValue(entry, initial, ApplyValueDisposition::kSilent);
  } else if (options.initial.has_value()) {
    LogIncompatibleStampedValue(
      "registration", entry.snapshot.definition, options.initial->value);
  }

  const auto [it, inserted]
    = cvars_.emplace(entry.snapshot.definition.name, std::move(entry));
  if (!inserted) {
    return CVarHandle {};
  }
  (void)ApplyCachedStartupValue(it->second);
  (void)ApplyCachedPersistedValue(it->second);
  return CVarHandle { it->second.id };
}

auto Registry::FindCVar(const std::string_view name) const
  -> observer_ptr<const CVarSnapshot>
{
  if (const auto it = cvars_.find(std::string(name)); it != cvars_.end()) {
    return make_observer(&it->second.snapshot);
  }
  return observer_ptr<const CVarSnapshot> {};
}

auto Registry::SetCVarFromText(
  const SetCVarRequest& request, const CommandContext& context) -> CommandResult
{
  if (const auto it = cvars_.find(std::string(request.name));
    it != cvars_.end()) {
    auto& cvar = it->second;
    const auto& snapshot = cvar.snapshot;
    if (HasFlag(snapshot.definition.flags, CVarFlags::kReadOnly)) {
      return {
        .status = ExecutionStatus::kDenied,
        .output = {},
        .error = "cvar is read-only",
      };
    }
    if (!IsCVarMutationAllowed(snapshot.definition, context)) {
      return {
        .status = ExecutionStatus::kDenied,
        .output = {},
        .error = "cvar denied by policy",
      };
    }

    CVarValue parsed;
    if (!TryParseValue(snapshot.current_value, request.text, parsed)) {
      return {
        .status = ExecutionStatus::kInvalidArguments,
        .output = {},
        .error = "value parse failed",
      };
    }
    if (!IsAllowedValue(snapshot.definition, parsed)) {
      return NotAllowedResult(snapshot.definition, request.text);
    }
    ClampValue(snapshot.definition, parsed);

    return ApplyStampedValue(cvar,
      StampedCVarValue {
        .value = std::move(parsed),
        .origin = CVarValueOrigin::kRuntimeExplicit,
      },
      ApplyValueDisposition::kReport)
      .result;
  }

  return {
    .status = ExecutionStatus::kNotFound,
    .output = {},
    .error = "cvar not found",
  };
}

auto Registry::ApplyStartupPlan(const ConsoleStartupPlan& startup_plan) -> void
{
  for (const auto& entry : startup_plan.Entries()) {
    startup_cvar_values_[entry.name] = entry.stamped_value;
    if (const auto it = cvars_.find(entry.name); it != cvars_.end()) {
      (void)ApplyCachedStartupValue(it->second);
    }
  }
}

auto Registry::ApplyLatchedCVars() -> size_t
{
  size_t applied = 0;
  for (auto& [_, cvar] : cvars_) {
    if (cvar.snapshot.latched_value.has_value()) {
      const auto previous_value = cvar.snapshot.current_value;
      const auto previous_origin = cvar.snapshot.current_origin;
      const auto name = cvar.snapshot.definition.name;
      cvar.snapshot.current_value = std::move(*cvar.snapshot.latched_value);
      cvar.snapshot.current_origin
        = cvar.latched_origin.value_or(cvar.snapshot.current_origin);
      const auto current_value_text
        = ValueToString(cvar.snapshot.current_value);
      const auto previous_value_text = ValueToString(previous_value);
      LOG_F(INFO,
        "CVar latched applied: name='{}' value='{}' origin={} "
        "previous_value='{}' previous_origin={}",
        name, current_value_text, cvar.snapshot.current_origin,
        previous_value_text, previous_origin);
      cvar.snapshot.latched_value.reset();
      cvar.latched_origin.reset();
      ++applied;
    }
  }
  return applied;
}

auto Registry::SaveArchiveCVars(const oxygen::PathFinder& path_finder,
  const ArchiveSaveMode mode) -> CommandResult
{
  json payload = json::object();
  payload[std::string(kArchiveJsonVersionKey)] = kArchiveJsonVersion1;
  payload[std::string(kArchiveJsonEntriesKey)] = json::array();

  auto& entries = payload[std::string(kArchiveJsonEntriesKey)];
  std::unordered_map<std::string, std::optional<CVarValue>> persisted_updates;
  for (auto& [name, cvar] : cvars_) {
    if (!HasFlag(cvar.snapshot.definition.flags, CVarFlags::kArchive)) {
      continue;
    }

    std::optional<CVarValue> persisted_value;
    switch (mode) {
    case ArchiveSaveMode::kAutomatic:
      if (cvar.snapshot.current_origin == CVarValueOrigin::kPersistedPreference
        || cvar.snapshot.current_origin == CVarValueOrigin::kRuntimeExplicit) {
        persisted_value = cvar.snapshot.current_value;
      } else if (cvar.persisted_value.has_value()) {
        persisted_value = cvar.persisted_value;
      }
      break;
    case ArchiveSaveMode::kExplicit:
      if (cvar.snapshot.current_origin == CVarValueOrigin::kAppForced) {
        if (cvar.persisted_value.has_value()) {
          persisted_value = cvar.persisted_value;
        }
      } else {
        persisted_value = cvar.snapshot.current_value;
      }
      break;
    }

    persisted_updates.emplace(name, persisted_value);
    if (!persisted_value.has_value()) {
      continue;
    }

    entries.push_back(json {
      { std::string(kArchiveJsonNameKey), name },
      {
        std::string(kArchiveJsonTypeKey),
        std::string(to_string(CVarTypeOf(*persisted_value))),
      },
      {
        std::string(kArchiveJsonValueKey),
        SerializeCVarValue(*persisted_value),
      },
    });
  }

  const auto archive_path = path_finder.CVarsArchivePath();
  const auto archive_dir = archive_path.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(archive_dir, ec);
  if (ec) {
    LOG_F(ERROR,
      "Console CVar persistence save failed: unable to create directory '{}': "
      "{}",
      archive_dir.generic_string(), ec.message());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "unable to create cvar archive directory",
    };
  }

  std::ofstream stream(archive_path);
  if (!stream.is_open()) {
    LOG_F(ERROR,
      "Console CVar persistence save failed: unable to open '{}' for write",
      archive_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "unable to open cvar archive file for write",
    };
  }
  stream << payload.dump(kJsonIndentSpaces);
  if (!stream.good()) {
    LOG_F(ERROR, "Console CVar persistence save failed: write error for '{}'",
      archive_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "failed to write cvar archive file",
    };
  }

  LOG_F(INFO, "Console CVars saved to '{}'", archive_path.generic_string());
  for (auto& [name, persisted_value] : persisted_updates) {
    if (const auto cvar_it = cvars_.find(name); cvar_it != cvars_.end()) {
      cvar_it->second.persisted_value = persisted_value;
    }
    if (persisted_value.has_value()) {
      persisted_cvar_values_[name] = *persisted_value;
    } else {
      persisted_cvar_values_.erase(name);
    }
  }
  return {
    .status = ExecutionStatus::kOk,
    .output = "saved cvar archive to " + archive_path.generic_string(),
    .error = {},
  };
}

auto Registry::LoadArchiveCVars(const oxygen::PathFinder& path_finder,
  [[maybe_unused]] const CommandContext& context) -> CommandResult
{
  const auto archive_path = path_finder.CVarsArchivePath();
  std::ifstream stream(archive_path);
  if (!stream.is_open()) {
    LOG_F(WARNING,
      "Console CVar persistence load skipped: archive file not found '{}'",
      archive_path.generic_string());
    return {
      .status = ExecutionStatus::kNotFound,
      .output = {},
      .error = "cvar archive file not found",
    };
  }

  json payload;
  try {
    stream >> payload;
  } catch (const std::exception& e) {
    LOG_F(ERROR,
      "Console CVar persistence load failed: json parse error in '{}': {}",
      archive_path.generic_string(), e.what());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "cvar archive json parse failed",
    };
  } catch (...) {
    LOG_F(ERROR,
      "Console CVar persistence load failed: unknown json parse error in '{}'",
      archive_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "cvar archive json parse failed",
    };
  }

  if (!payload.is_object()
    || !payload.contains(std::string(kArchiveJsonEntriesKey))
    || !payload[std::string(kArchiveJsonEntriesKey)].is_array()) {
    LOG_F(ERROR, "Console CVar persistence load failed: invalid schema in '{}'",
      archive_path.generic_string());
    return {
      .status = ExecutionStatus::kInvalidArguments,
      .output = {},
      .error = "invalid cvar archive schema",
    };
  }

  persisted_cvar_values_.clear();
  for (auto& [_, cvar] : cvars_) {
    cvar.persisted_value.reset();
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
    if (!entry.contains(std::string(kArchiveJsonTypeKey))
      || !entry[std::string(kArchiveJsonTypeKey)].is_string()) {
      continue;
    }
    if (!entry.contains(std::string(kArchiveJsonValueKey))) {
      continue;
    }

    const auto name
      = entry[std::string(kArchiveJsonNameKey)].get<std::string>();
    const auto type_name
      = entry[std::string(kArchiveJsonTypeKey)].get<std::string>();
    CVarType type {};
    if (!TryParseCVarTypeName(type_name, type)) {
      continue;
    }

    CVarValue value;
    if (!DeserializeCVarValue(
          type, entry[std::string(kArchiveJsonValueKey)], value)) {
      continue;
    }

    persisted_cvar_values_[name] = value;

    const auto cvar_it = cvars_.find(name);
    if (cvar_it == cvars_.end()) {
      continue;
    }
    if (ApplyCachedPersistedValue(cvar_it->second)) {
      ++applied;
    }
  }

  LOG_F(INFO, "Console CVars loaded from '{}': {} override(s) applied",
    archive_path.generic_string(), applied);
  return {
    .status = ExecutionStatus::kOk,
    .output = "loaded " + std::to_string(applied) + " cvar override(s) from "
      + archive_path.generic_string(),
    .error = {},
  };
}

auto Registry::ApplyCachedStartupValue(CVarEntry& cvar) -> bool
{
  const auto it = startup_cvar_values_.find(cvar.snapshot.definition.name);
  if (it == startup_cvar_values_.end()) {
    return false;
  }
  // A text value for a typed variable comes from a command line: parse it
  // as the console would.
  if (const auto* text = std::get_if<std::string>(&it->second.value);
    text != nullptr
    && !IsCompatibleValueType(cvar.snapshot.definition, it->second.value)) {
    auto parsed = CVarValue {};
    if (TryParseValue(cvar.snapshot.current_value, *text, parsed)
      && IsAllowedValue(cvar.snapshot.definition, parsed)) {
      it->second.value = std::move(parsed);
    }
  }
  if (!IsCompatibleValueType(cvar.snapshot.definition, it->second.value)) {
    LogIncompatibleStampedValue(
      "startup", cvar.snapshot.definition, it->second.value);
    return false;
  }

  auto stamped_value = it->second;
  ClampValue(cvar.snapshot.definition, stamped_value.value);
  return ApplyStampedValue(cvar, stamped_value, ApplyValueDisposition::kSilent)
    .applied;
}

auto Registry::ApplyCachedPersistedValue(CVarEntry& cvar) -> bool
{
  if (!HasFlag(cvar.snapshot.definition.flags, CVarFlags::kArchive)) {
    return false;
  }

  const auto it = persisted_cvar_values_.find(cvar.snapshot.definition.name);
  if (it == persisted_cvar_values_.end()) {
    return false;
  }
  if (!IsCompatibleValueType(cvar.snapshot.definition, it->second)) {
    LogIncompatibleStampedValue(
      "persisted", cvar.snapshot.definition, it->second);
    return false;
  }

  cvar.persisted_value = it->second;
  auto stamped_value = StampedCVarValue {
    .value = it->second,
    .origin = CVarValueOrigin::kPersistedPreference,
  };
  ClampValue(cvar.snapshot.definition, stamped_value.value);
  return ApplyStampedValue(cvar, stamped_value, ApplyValueDisposition::kSilent)
    .applied;
}

auto Registry::SaveHistory(const oxygen::PathFinder& path_finder) const
  -> CommandResult
{
  json payload = json::object();
  payload[std::string(kHistoryJsonVersionKey)] = kHistoryJsonVersion1;
  payload[std::string(kHistoryJsonEntriesKey)] = history_.Entries();

  const auto history_path = HistoryPathFor(path_finder);
  const auto history_dir = history_path.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(history_dir, ec);
  if (ec) {
    LOG_F(ERROR,
      "Console history persistence save failed: unable to create directory "
      "'{}': {}",
      history_dir.generic_string(), ec.message());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "unable to create history directory",
    };
  }

  std::ofstream stream(history_path);
  if (!stream.is_open()) {
    LOG_F(ERROR,
      "Console history persistence save failed: unable to open '{}' for write",
      history_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "unable to open history file for write",
    };
  }
  stream << payload.dump(kJsonIndentSpaces);
  if (!stream.good()) {
    LOG_F(ERROR,
      "Console history persistence save failed: write error for '{}'",
      history_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "failed to write history file",
    };
  }

  LOG_F(INFO, "Console history saved to '{}' ({} entries)",
    history_path.generic_string(), history_.Entries().size());
  return {
    .status = ExecutionStatus::kOk,
    .output = "saved console history to " + history_path.generic_string(),
    .error = {},
  };
}

auto Registry::LoadHistory(const oxygen::PathFinder& path_finder)
  -> CommandResult
{
  const auto history_path = HistoryPathFor(path_finder);
  std::ifstream stream(history_path);
  if (!stream.is_open()) {
    LOG_F(WARNING,
      "Console history persistence load skipped: history file not found '{}'",
      history_path.generic_string());
    return {
      .status = ExecutionStatus::kNotFound,
      .output = {},
      .error = "console history file not found",
    };
  }

  json payload;
  try {
    stream >> payload;
  } catch (const std::exception& e) {
    LOG_F(ERROR,
      "Console history persistence load failed: json parse error in '{}': {}",
      history_path.generic_string(), e.what());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "console history json parse failed",
    };
  } catch (...) {
    LOG_F(ERROR,
      "Console history persistence load failed: unknown json parse error in "
      "'{}'",
      history_path.generic_string());
    return {
      .status = ExecutionStatus::kError,
      .output = {},
      .error = "console history json parse failed",
    };
  }

  if (!payload.is_object()
    || !payload.contains(std::string(kHistoryJsonEntriesKey))
    || !payload[std::string(kHistoryJsonEntriesKey)].is_array()) {
    LOG_F(ERROR,
      "Console history persistence load failed: invalid schema in '{}'",
      history_path.generic_string());
    return {
      .status = ExecutionStatus::kInvalidArguments,
      .output = {},
      .error = "invalid console history schema",
    };
  }

  history_.Clear();
  size_t loaded = 0;
  for (const auto& entry : payload[std::string(kHistoryJsonEntriesKey)]) {
    if (!entry.is_string()) {
      continue;
    }
    history_.Push(entry.get<std::string>());
    ++loaded;
  }

  LOG_F(INFO, "Console history loaded from '{}': {} entr{}",
    history_path.generic_string(), loaded, loaded == 1 ? "y" : "ies");
  return {
    .status = ExecutionStatus::kOk,
    .output = "loaded " + std::to_string(loaded) + " history entries from "
      + history_path.generic_string(),
    .error = {},
  };
}

auto Registry::ApplyCommandLineOverrides(
  const std::span<const std::string_view> arguments,
  const CommandContext& context) -> CommandResult
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
        .output = {},
        .error = "empty command line cvar override",
      };
    }

    auto assign = CommandLineAssign {};
    if (!ParseCommandLineAssign(token, assign)) {
      if (i + 1 >= arguments.size()) {
        return {
          .status = ExecutionStatus::kInvalidArguments,
          .output = {},
          .error = "command line override missing value",
        };
      }
      assign.name = token;
      assign.value = arguments[++i];
    }

    const auto cvar_it = cvars_.find(std::string(assign.name));
    if (cvar_it == cvars_.end()) {
      return {
        .status = ExecutionStatus::kNotFound,
        .output = {},
        .error = "cvar not found",
      };
    }

    auto& cvar = cvar_it->second;
    if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kReadOnly)) {
      return {
        .status = ExecutionStatus::kDenied,
        .output = {},
        .error = "cvar is read-only",
      };
    }
    if (!IsCVarMutationAllowed(cvar.snapshot.definition, context)) {
      return {
        .status = ExecutionStatus::kDenied,
        .output = {},
        .error = "cvar denied by policy",
      };
    }

    CVarValue parsed;
    if (!TryParseValue(cvar.snapshot.current_value, assign.value, parsed)) {
      return {
        .status = ExecutionStatus::kInvalidArguments,
        .output = {},
        .error = "value parse failed",
      };
    }
    if (!IsAllowedValue(cvar.snapshot.definition, parsed)) {
      return NotAllowedResult(cvar.snapshot.definition, assign.value);
    }
    ClampValue(cvar.snapshot.definition, parsed);

    const auto result = ApplyStampedValue(cvar,
      StampedCVarValue {
        .value = std::move(parsed),
        .origin = CVarValueOrigin::kStartupExplicit,
      },
      ApplyValueDisposition::kReport);
    if (result.result.status != ExecutionStatus::kOk) {
      return result.result;
    }
    if (result.applied) {
      ++applied;
    }
  }

  return {
    .status = ExecutionStatus::kOk,
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
  case CommandSource::kUi:
  case CommandSource::kKey:
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

auto Registry::Parse(const std::string_view line) const
  -> Result<std::vector<CommandInvocation>, CommandResult>
{
  const auto lines = Parser::SplitCommands(line);
  if (lines.empty()) {
    return Err(Failure(ExecutionStatus::kError, "empty command"));
  }
  auto invocations = std::vector<CommandInvocation> {};
  for (const auto& command_line : lines) {
    auto tokens = Parser::Tokenize(command_line);
    if (tokens.empty()) {
      return Err(Failure(ExecutionStatus::kError, "empty command"));
    }
    if (cvars_.contains(tokens.front())) {
      // A variable name alone reads it; followed by a value, it sets it.
      const auto verb = tokens.size() == 1 ? kCVarGetCommand : kCVarSetCommand;
      tokens.insert(tokens.begin(), std::string(verb));
    }
    const auto name = tokens.front();
    tokens.erase(tokens.begin());
    auto invocation = commands_->BindText(name, tokens);
    if (!invocation) {
      auto failure = std::move(invocation).error();
      if (failure.status == ExecutionStatus::kNotFound) {
        failure.error = "unknown command/cvar: " + name;
      }
      return Err(std::move(failure));
    }
    invocations.push_back(std::move(invocation).value());
  }
  return Ok(std::move(invocations));
}

auto Registry::Execute(
  const std::string_view line, const CommandContext& context) -> CommandResult
{
  if (context.source == CommandSource::kLocalConsole) {
    RecordHistory(line);
  }
  auto result = CommandResult::Success();
  auto invocations = Parse(line);
  if (!invocations) {
    result = std::move(invocations).error();
  } else {
    for (const auto& invocation : invocations.value()) {
      result = commands_->Execute(invocation, context);
      if (result.status != ExecutionStatus::kOk) {
        break;
      }
      RecordUsage(invocation);
    }
  }
  RecordExecution(std::string(line), result);
  return result;
}

auto Registry::RecordHistory(const std::string_view line) -> void
{
  history_.Push(std::string(line));
}

auto Registry::RecordUsage(const CommandInvocation& invocation) -> void
{
  RecordCompletionUsage(CompletionToken(invocation));
}

auto Registry::RecordExecution(std::string line, CommandResult result) -> void
{
  if (execution_record_capacity_ == 0) {
    return;
  }
  if (execution_records_.size() >= execution_record_capacity_) {
    execution_records_.erase(execution_records_.begin());
  }
  execution_records_.push_back(ExecutionRecord {
    .line = std::move(line),
    .result = std::move(result),
  });
}

auto Registry::Complete(const std::string_view prefix) const
  -> std::vector<CompletionCandidate>
{
  std::vector<CompletionCandidate> out;

  for (const auto& command : commands_->List()) {
    if (StartsWithCaseInsensitive(
          { .text = command->name, .prefix = prefix })) {
      out.push_back({
        .kind = CompletionKind::kCommand,
        .token = command->name,
        .help = command->help,
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

  std::ranges::sort(out, [this](const auto& lhs, const auto& rhs) -> bool {
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

auto Registry::ListSymbols(const bool include_hidden) const
  -> std::vector<ConsoleSymbol>
{
  std::vector<ConsoleSymbol> out;
  const auto commands = commands_->List();
  out.reserve(commands.size() + cvars_.size());

  for (const auto& command : commands) {
    const auto& name = command->name;
    const auto usage_it = completion_usage_.find(name);
    const auto usage_frequency
      = usage_it != completion_usage_.end() ? usage_it->second.frequency : 0;
    const auto usage_last_tick = usage_it != completion_usage_.end()
      ? usage_it->second.last_used_tick
      : 0;
    out.push_back(ConsoleSymbol {
      .kind = CompletionKind::kCommand,
      .token = name,
      .help = command->help,
      .usage_frequency = usage_frequency,
      .usage_last_tick = usage_last_tick,
    });
  }
  for (const auto& [name, cvar] : cvars_) {
    if (!include_hidden && IsHidden(cvar.snapshot.definition)) {
      continue;
    }
    const auto usage_it = completion_usage_.find(name);
    const auto usage_frequency
      = usage_it != completion_usage_.end() ? usage_it->second.frequency : 0;
    const auto usage_last_tick = usage_it != completion_usage_.end()
      ? usage_it->second.last_used_tick
      : 0;
    out.push_back(ConsoleSymbol {
      .kind = CompletionKind::kCVar,
      .token = name,
      .help = cvar.snapshot.definition.help,
      .usage_frequency = usage_frequency,
      .usage_last_tick = usage_last_tick,
    });
  }

  std::ranges::sort(
    out, [](const ConsoleSymbol& lhs, const ConsoleSymbol& rhs) -> bool {
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

auto Registry::GetExecutionRecords() const
  -> const std::vector<ExecutionRecord>&
{
  return execution_records_;
}

auto Registry::ClearExecutionRecords() -> void { execution_records_.clear(); }

void Registry::RegisterBuiltinCommands()
{
  const auto add = [this](CommandDefinition definition) -> void {
    const auto handle = commands_->Register(std::move(definition));
    if (handle.get() != 0U) {
      builtin_handles_.push_back(handle);
    }
  };

  add(CommandDefinition {
    .name = std::string(kBuiltinHelpCommand),
    .label = "Help",
    .help = "Show help for a command or console variable",
    .params = {
      CommandParam {
        .name = "name",
        .type = ParamType::kString,
        .help = "Command or variable; empty shows console usage",
        .default_value = ParamValue { std::string {} },
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto& name = args.GetString("name");
      if (name.empty()) {
        return CommandResult::Success(
          "usage:\n"
          "  help [name]                 help for a command or variable\n"
          "  list [all|commands|cvars]   list commands and variables\n"
          "  find <pattern>              search commands and variables\n"
          "  exec <path>                 run a file of console lines\n"
          "  <cvar> [value]              read or set a variable");
      }
      if (const auto command = commands_->Find(name); command != nullptr) {
        auto lines = std::vector<std::string> {
          fmt::format("command {} ({}) flags={}: {}", command->name,
            command->label, commands::to_string(command->flags),
            command->help),
        };
        for (const auto& param : command->params) {
          lines.push_back(fmt::format("  {} {}{}{}", param.name,
            commands::to_string(param.type),
            param.required ? " required" : "",
            param.help.empty() ? std::string {} : " - " + param.help));
        }
        return CommandResult::Success(fmt::format("{}", fmt::join(lines, "\n")),
          commands_->Describe(*command, CommandContext {}));
      }
      if (const auto cvar = FindCVar(name);
        cvar != nullptr && !IsHidden(cvar->definition)) {
        return CommandResult::Success("cvar " + cvar->definition.name
          + " flags=" + to_string(cvar->definition.flags)
          + " default=" + ValueToString(cvar->definition.default_value)
          + " current=" + ValueToString(cvar->current_value) + " : "
          + cvar->definition.help);
      }
      return Failure(ExecutionStatus::kNotFound, "name not found");
    },
  });

  add(CommandDefinition {
    .name = std::string(kBuiltinFindCommand),
    .label = "Find",
    .help = "Search commands and console variables by name or help text",
    .params = {
      CommandParam {
        .name = "pattern",
        .type = ParamType::kString,
        .required = true,
        .rest_of_line = true,
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto& pattern = args.GetString("pattern");
      auto matches = std::vector<std::string> {};
      for (const auto& command : commands_->List()) {
        if (ContainsCaseInsensitive({ .text = command->name, .needle = pattern })
          || ContainsCaseInsensitive(
            { .text = command->help, .needle = pattern })) {
          matches.push_back("cmd  " + command->name + " - " + command->help);
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
        return Failure(ExecutionStatus::kNotFound, "no matches");
      }
      std::ranges::sort(matches);
      return CommandResult::Success(
        fmt::format("{}", fmt::join(matches, "\n")));
    },
  });

  add(CommandDefinition {
    .name = std::string(kBuiltinListCommand),
    .label = "List",
    .help = "List commands, console variables, or both",
    .params = {
      CommandParam {
        .name = "mode",
        .type = ParamType::kEnum,
        .default_value = ParamValue { std::string(kListModeAll) },
        .allowed_values = { std::string(kListModeAll),
          std::string(kListModeCommands), std::string(kListModeCVars), },
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto& mode = args.GetString("mode");
      auto lines = std::vector<std::string> {};
      if (mode != kListModeCVars) {
        for (const auto& command : commands_->List()) {
          lines.push_back("cmd  " + command->name + " - " + command->help);
        }
      }
      if (mode != kListModeCommands) {
        for (const auto& [name, cvar] : cvars_) {
          if (!IsHidden(cvar.snapshot.definition)) {
            lines.push_back(
              "cvar " + name + " - " + cvar.snapshot.definition.help);
          }
        }
      }
      std::ranges::sort(lines);
      return CommandResult::Success(fmt::format("{}", fmt::join(lines, "\n")));
    },
  });

  add(CommandDefinition {
    .name = std::string(kBuiltinExecCommand),
    .label = "Run Script",
    .help = "Run a file of console lines, one per line",
    .params = {
      CommandParam {
        .name = "path",
        .type = ParamType::kPath,
        .required = true,
        .rest_of_line = true,
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext& context) -> CommandResult {
      return ExecuteScriptFile(args.GetString("path"), context);
    },
  });

  add(CommandDefinition {
    .name = std::string(kCVarGetCommand),
    .label = "Get Variable",
    .help = "Read a console variable",
    .params = {
      CommandParam {
        .name = "name",
        .type = ParamType::kString,
        .required = true,
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto cvar = FindCVar(args.GetString("name"));
      if (cvar == nullptr) {
        return Failure(ExecutionStatus::kNotFound, "cvar not found");
      }
      return CommandResult::Success(
        cvar->definition.name + " = " + ValueToString(cvar->current_value),
        DescribeCVar(*cvar));
    },
  });

  add(CommandDefinition {
    .name = std::string(kCVarSetCommand),
    .label = "Set Variable",
    .help = "Set a console variable; the value is parsed for its type",
    .params = {
      CommandParam {
        .name = "name",
        .type = ParamType::kString,
        .required = true,
      },
      CommandParam {
        .name = "value",
        .type = ParamType::kScalar,
        .required = true,
        .rest_of_line = true,
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext& context) -> CommandResult {
      const auto text = ScalarText(*args.Find("value"));
      return SetCVarFromText(
        { .name = args.GetString("name"), .text = text }, context);
    },
  });

  add(CommandDefinition {
    .name = std::string(kCVarListCommand),
    .label = "List Variables",
    .help = "List console variables whose name contains the pattern",
    .params = {
      CommandParam {
        .name = "pattern",
        .type = ParamType::kString,
        .default_value = ParamValue { std::string {} },
      },
    },
    .handler = [this](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const auto& pattern = args.GetString("pattern");
      auto described = json::array();
      auto lines = std::vector<std::string> {};
      for (const auto& [name, cvar] : cvars_) {
        if (IsHidden(cvar.snapshot.definition)
          || !ContainsCaseInsensitive({ .text = name, .needle = pattern })) {
          continue;
        }
        described.push_back(DescribeCVar(cvar.snapshot));
        lines.push_back(
          name + " = " + ValueToString(cvar.snapshot.current_value));
      }
      std::ranges::sort(lines);
      return CommandResult::Success(
        fmt::format("{}", fmt::join(lines, "\n")), std::move(described));
    },
  });
}

auto Registry::ExecuteScriptFile(
  const std::string_view path, const CommandContext& context) -> CommandResult
{
  if (script_depth_ >= kMaxScriptDepth) {
    return {
      .status = ExecutionStatus::kDenied,
      .output = {},
      .error = "max script depth exceeded",
    };
  }

  std::ifstream script { std::string(path) };
  if (!script.is_open()) {
    return {
      .status = ExecutionStatus::kNotFound,
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
  CommandResult last {
    .status = ExecutionStatus::kOk,
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
  const auto clamp_double = [&](double& v) -> void {
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

auto Registry::OriginRank(const CVarValueOrigin origin) noexcept -> int
{
  switch (origin) {
  case CVarValueOrigin::kAppDefault:
    return 0;
  case CVarValueOrigin::kPersistedPreference:
    return 1;
  case CVarValueOrigin::kStartupExplicit:
    return 2;
  case CVarValueOrigin::kRuntimeExplicit:
    return 3;
  case CVarValueOrigin::kAppForced:
    return 4;
  }
  return -1;
}

auto Registry::ApplyStampedValue(CVarEntry& cvar,
  const StampedCVarValue& stamped_value,
  const ApplyValueDisposition disposition) -> ApplyValueResult
{
  const auto make_result
    = [&](const ExecutionStatus status, std::string output, std::string error,
        const bool applied) -> ApplyValueResult {
    return {
      .result = CommandResult {
        .status = status,
        .output = std::move(output),
        .error = std::move(error),
      },
      .applied = applied,
    };
  };

  const auto active_origin = [&] -> CVarValueOrigin {
    if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kLatched)
      && cvar.latched_origin.has_value()) {
      return *cvar.latched_origin;
    }
    if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kRequiresRestart)
      && cvar.restart_origin.has_value()) {
      return *cvar.restart_origin;
    }
    return cvar.snapshot.current_origin;
  }();
  const auto active_value = [&] -> const CVarValue& {
    if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kLatched)
      && cvar.snapshot.latched_value.has_value()) {
      return *cvar.snapshot.latched_value;
    }
    if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kRequiresRestart)
      && cvar.snapshot.restart_value.has_value()) {
      return *cvar.snapshot.restart_value;
    }
    return cvar.snapshot.current_value;
  }();

  if (OriginRank(stamped_value.origin) < OriginRank(active_origin)) {
    const auto name = cvar.snapshot.definition.name;
    const auto incoming_value_text = ValueToString(stamped_value.value);
    const auto active_value_text = ValueToString(active_value);
    LOG_F(INFO,
      "CVar ignored: name='{}' incoming_value='{}' incoming_origin={} "
      "active_value='{}' active_origin={} reason='higher-precedence value'",
      name, incoming_value_text, stamped_value.origin, active_value_text,
      active_origin);
    const auto output = disposition == ApplyValueDisposition::kReport
      ? "ignored " + cvar.snapshot.definition.name
        + " due to higher-precedence value"
      : std::string {};
    return make_result(ExecutionStatus::kOk, output, {}, false);
  }

  if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kLatched)) {
    if (cvar.snapshot.latched_value.has_value()
      && *cvar.snapshot.latched_value == stamped_value.value
      && cvar.latched_origin == stamped_value.origin) {
      const auto output = disposition == ApplyValueDisposition::kReport
        ? "latched " + cvar.snapshot.definition.name + " = "
          + ValueToString(*cvar.snapshot.latched_value)
        : std::string {};
      return make_result(ExecutionStatus::kOk, output, {}, false);
    }
    const auto had_staged_value = cvar.snapshot.latched_value.has_value();
    const auto previous_staged_value = had_staged_value
      ? ValueToString(*cvar.snapshot.latched_value)
      : std::string("<none>");
    const auto previous_staged_origin_set = cvar.latched_origin.has_value();
    const auto previous_staged_origin
      = cvar.latched_origin.value_or(CVarValueOrigin::kAppDefault);
    const auto name = cvar.snapshot.definition.name;
    cvar.snapshot.latched_value = stamped_value.value;
    cvar.latched_origin = stamped_value.origin;
    const auto staged_value_text = ValueToString(*cvar.snapshot.latched_value);
    const auto live_value_text = ValueToString(cvar.snapshot.current_value);
    LOG_F(INFO,
      "CVar latched staged: name='{}' staged_value='{}' staged_origin={} "
      "previous_staged_value='{}' previous_staged_origin_set={} "
      "previous_staged_origin={} live_value='{}' live_origin={}",
      name, staged_value_text, *cvar.latched_origin, previous_staged_value,
      previous_staged_origin_set, previous_staged_origin, live_value_text,
      cvar.snapshot.current_origin);
    const auto output = disposition == ApplyValueDisposition::kReport
      ? "latched " + cvar.snapshot.definition.name + " = "
        + ValueToString(*cvar.snapshot.latched_value)
      : std::string {};
    return make_result(ExecutionStatus::kOk, output, {}, true);
  }

  if (HasFlag(cvar.snapshot.definition.flags, CVarFlags::kRequiresRestart)) {
    if (cvar.snapshot.restart_value.has_value()
      && *cvar.snapshot.restart_value == stamped_value.value
      && cvar.restart_origin == stamped_value.origin) {
      const auto output = disposition == ApplyValueDisposition::kReport
        ? "restart required for " + cvar.snapshot.definition.name + " = "
          + ValueToString(*cvar.snapshot.restart_value)
        : std::string {};
      return make_result(ExecutionStatus::kOk, output, {}, false);
    }
    const auto previous_staged_value = cvar.snapshot.restart_value.has_value()
      ? ValueToString(*cvar.snapshot.restart_value)
      : std::string("<none>");
    const auto previous_staged_origin_set = cvar.restart_origin.has_value();
    const auto previous_staged_origin
      = cvar.restart_origin.value_or(CVarValueOrigin::kAppDefault);
    const auto name = cvar.snapshot.definition.name;
    cvar.snapshot.restart_value = stamped_value.value;
    cvar.restart_origin = stamped_value.origin;
    const auto staged_value_text = ValueToString(*cvar.snapshot.restart_value);
    const auto live_value_text = ValueToString(cvar.snapshot.current_value);
    LOG_F(INFO,
      "CVar restart staged: name='{}' staged_value='{}' staged_origin={} "
      "previous_staged_value='{}' previous_staged_origin_set={} "
      "previous_staged_origin={} live_value='{}' live_origin={}",
      name, staged_value_text, *cvar.restart_origin, previous_staged_value,
      previous_staged_origin_set, previous_staged_origin, live_value_text,
      cvar.snapshot.current_origin);
    const auto output = disposition == ApplyValueDisposition::kReport
      ? "restart required for " + cvar.snapshot.definition.name + " = "
        + ValueToString(*cvar.snapshot.restart_value)
      : std::string {};
    return make_result(ExecutionStatus::kOk, output, {}, true);
  }

  if (cvar.snapshot.current_value == stamped_value.value
    && cvar.snapshot.current_origin == stamped_value.origin) {
    const auto output = disposition == ApplyValueDisposition::kReport
      ? cvar.snapshot.definition.name + " = "
        + ValueToString(cvar.snapshot.current_value)
      : std::string {};
    return make_result(ExecutionStatus::kOk, output, {}, false);
  }

  const auto previous_value = cvar.snapshot.current_value;
  const auto previous_origin = cvar.snapshot.current_origin;
  const auto name = cvar.snapshot.definition.name;
  cvar.snapshot.current_value = stamped_value.value;
  cvar.snapshot.current_origin = stamped_value.origin;
  const auto current_value_text = ValueToString(cvar.snapshot.current_value);
  const auto previous_value_text = ValueToString(previous_value);
  LOG_F(INFO,
    "CVar applied: name='{}' value='{}' origin={} previous_value='{}' "
    "previous_origin={}",
    name, current_value_text, cvar.snapshot.current_origin, previous_value_text,
    previous_origin);
  const auto output = disposition == ApplyValueDisposition::kReport
    ? cvar.snapshot.definition.name + " = "
      + ValueToString(cvar.snapshot.current_value)
    : std::string {};
  return make_result(ExecutionStatus::kOk, output, {}, true);
}

auto Registry::SourcePolicyFor(const CommandSource source) const
  -> const SourcePolicy&
{
  switch (source) {
  case CommandSource::kLocalConsole:
  case CommandSource::kUi:
  case CommandSource::kKey:
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

auto Registry::IsCVarMutationAllowed(
  const CVarDefinition& definition, const CommandContext& context) const -> bool
{
  const auto& policy = SourcePolicyFor(context.source);
  if (!policy.allow_cvars) {
    return false;
  }

  if (context.shipping_build
    && (HasFlag(definition.flags, CVarFlags::kCheat)
      || HasFlag(definition.flags, CVarFlags::kDevOnly))) {
    return false;
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

} // namespace oxygen::console
