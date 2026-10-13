//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include <fmt/format.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

namespace oxygen::examples {

namespace {

  using commands::CommandArgs;
  using commands::CommandContext;
  using commands::CommandParam;
  using commands::CommandResult;
  using commands::ParamType;
  using commands::ParamValue;

  constexpr const char* kValue = "value";

  auto ValueParam(const ParamType type, std::string help) -> CommandParam
  {
    return CommandParam {
      .name = std::string(kValue),
      .type = type,
      .help = std::move(help),
      .required = true,
    };
  }

  //! The double nearest to the shortest decimal that round-trips `value`,
  //! so a schema shows `0.2` rather than `0.20000000298023224`.
  auto ToSchemaNumber(const float value) -> double
  {
    std::array<char, 32> text {};
    const auto written
      = std::to_chars(text.data(), text.data() + text.size(), value);
    double result {};
    std::from_chars(text.data(), written.ptr, result);
    return result;
  }

  auto SetLimits(CommandParam& param, const std::optional<float> min,
    const std::optional<float> max) -> void
  {
    if (min) {
      param.min_value = ToSchemaNumber(*min);
    }
    if (max) {
      param.max_value = ToSchemaNumber(*max);
    }
  }

  auto NumberParam(std::string name, std::string help,
    const std::optional<float> min, const std::optional<float> max)
    -> CommandParam
  {
    auto param = CommandParam {
      .name = std::move(name),
      .type = ParamType::kDouble,
      .help = std::move(help),
      .required = true,
    };
    SetLimits(param, min, max);
    return param;
  }

  auto ToFloat(const CommandArgs& args, const std::string_view name) -> float
  {
    return static_cast<float>(args.GetDouble(name));
  }

} // namespace

PanelCommands::PanelCommands(
  const observer_ptr<commands::CommandRegistry> registry,
  const observer_ptr<control::CommandDispatcher> dispatcher)
  : registry_(registry)
  , dispatcher_(dispatcher)
{
  CHECK_NOTNULL_F(registry_, "PanelCommands requires a command registry");
}

PanelCommands::~PanelCommands()
{
  for (const auto handle : handles_) {
    static_cast<void>(registry_->Unregister(handle));
  }
}

auto PanelCommands::Add(commands::CommandDefinition definition) -> void
{
  const auto name = definition.name;
  const auto handle = registry_->Register(std::move(definition));
  if (!handle.IsValid()) {
    throw std::runtime_error("Cannot register panel command: " + name);
  }
  handles_.push_back(handle);
}

auto PanelCommands::AddAction(std::string name, std::string label,
  std::string help, std::function<void()> action) -> void
{
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .handler = [action = std::move(action)](
                 const CommandArgs&, const CommandContext&) -> CommandResult {
      action();
      return CommandResult::Success();
    },
  });
}

auto PanelCommands::AddBool(std::string name, std::string label,
  std::string help, std::function<void(bool)> set) -> void
{
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = { ValueParam(ParamType::kBool, "true or false") },
    .handler
    = [command = std::move(command), set = std::move(set)](
        const CommandArgs& args, const CommandContext&) -> CommandResult {
      const bool value = args.GetBool(kValue);
      set(value);
      return CommandResult::Success(fmt::format("{} = {}", command, value));
    },
  });
}

auto PanelCommands::AddInt(std::string name, std::string label,
  std::string help, std::function<void(int)> set, const std::optional<int> min,
  const std::optional<int> max) -> void
{
  auto param = ValueParam(ParamType::kInt, "Integer value");
  if (min) {
    param.min_value = static_cast<double>(*min);
  }
  if (max) {
    param.max_value = static_cast<double>(*max);
  }
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = { std::move(param) },
    .handler
    = [command = std::move(command), set = std::move(set)](
        const CommandArgs& args, const CommandContext&) -> CommandResult {
      const auto value = static_cast<int>(args.GetInt(kValue));
      set(value);
      return CommandResult::Success(fmt::format("{} = {}", command, value));
    },
  });
}

auto PanelCommands::AddFloat(std::string name, std::string label,
  std::string help, std::function<void(float)> set,
  const std::optional<float> min, const std::optional<float> max) -> void
{
  auto param = ValueParam(ParamType::kDouble, "Number");
  SetLimits(param, min, max);
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = { std::move(param) },
    .handler
    = [command = std::move(command), set = std::move(set)](
        const CommandArgs& args, const CommandContext&) -> CommandResult {
      const float value = ToFloat(args, kValue);
      set(value);
      return CommandResult::Success(fmt::format("{} = {}", command, value));
    },
  });
}

auto PanelCommands::AddEnum(std::string name, std::string label,
  std::string help, std::vector<std::string> values,
  std::function<void(std::size_t)> set) -> void
{
  auto param = ValueParam(ParamType::kEnum, "One of the allowed values");
  param.allowed_values = values;
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = { std::move(param) },
    .handler = [command = std::move(command), values = std::move(values),
                 set = std::move(set)](const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      // The binder accepts only allowed values.
      const auto& value = args.GetString(kValue);
      const auto found = std::ranges::find(values, value);
      CHECK_F(
        found != values.end(), "{}: '{}' passed the binder", command, value);
      set(static_cast<std::size_t>(std::distance(values.begin(), found)));
      return CommandResult::Success(fmt::format("{} = {}", command, value));
    },
  });
}

auto PanelCommands::AddText(std::string name, std::string label,
  std::string help, std::function<void(const std::string&)> set) -> void
{
  auto param = ValueParam(ParamType::kString, "Text");
  param.rest_of_line = true;
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = { std::move(param) },
    .handler
    = [command = std::move(command), set = std::move(set)](
        const CommandArgs& args, const CommandContext&) -> CommandResult {
      const auto& value = args.GetString(kValue);
      set(value);
      return CommandResult::Success(fmt::format("{} = {}", command, value));
    },
  });
}

auto PanelCommands::AddColor(std::string name, std::string label,
  std::string help, std::function<void(const graphics::Color&)> set) -> void
{
  auto alpha = NumberParam("a", "Alpha", 0.0F, 1.0F);
  alpha.required = false;
  alpha.default_value = ParamValue { 1.0 };
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = {
      NumberParam("r", "Red", 0.0F, std::nullopt),
      NumberParam("g", "Green", 0.0F, std::nullopt),
      NumberParam("b", "Blue", 0.0F, std::nullopt),
      std::move(alpha),
    },
    .handler = [command = std::move(command), set = std::move(set)](
                 const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const graphics::Color color { ToFloat(args, "r"), ToFloat(args, "g"),
        ToFloat(args, "b"), ToFloat(args, "a") };
      set(color);
      return CommandResult::Success(fmt::format("{} = ({}, {}, {}, {})",
        command, color.r, color.g, color.b, color.a));
    },
  });
}

auto PanelCommands::AddRgb(std::string name, std::string label,
  std::string help, std::function<void(const glm::vec3&)> set,
  const std::optional<float> max) -> void
{
  AddTriple(std::move(name), std::move(label), std::move(help),
    { "r", "g", "b" }, std::move(set), 0.0F, max);
}

auto PanelCommands::AddVec3(std::string name, std::string label,
  std::string help, std::function<void(const glm::vec3&)> set,
  const std::optional<float> min, const std::optional<float> max) -> void
{
  AddTriple(std::move(name), std::move(label), std::move(help),
    { "x", "y", "z" }, std::move(set), min, max);
}

auto PanelCommands::AddTriple(std::string name, std::string label,
  std::string help, const std::array<const char*, 3> components,
  std::function<void(const glm::vec3&)> set, const std::optional<float> min,
  const std::optional<float> max) -> void
{
  auto command = name;
  Add({
    .name = std::move(name),
    .label = std::move(label),
    .help = std::move(help),
    .params = {
      NumberParam(components[0], components[0], min, max),
      NumberParam(components[1], components[1], min, max),
      NumberParam(components[2], components[2], min, max),
    },
    .handler = [command = std::move(command), components, set = std::move(set)](
                 const CommandArgs& args,
                 const CommandContext&) -> CommandResult {
      const glm::vec3 value { ToFloat(args, components[0]),
        ToFloat(args, components[1]), ToFloat(args, components[2]) };
      set(value);
      return CommandResult::Success(fmt::format(
        "{} = ({}, {}, {})", command, value.x, value.y, value.z));
    },
  });
}

auto PanelCommands::Run(
  const std::string_view name, const nlohmann::json& arguments) const -> void
{
  auto invocation = registry_->BindJson(name, arguments);
  if (!invocation) {
    LOG_F(
      ERROR, "panel command '{}' rejected: {}", name, invocation.error().error);
    return;
  }
  const auto context
    = CommandContext { .source = commands::CommandSource::kUi };
  if (dispatcher_) {
    static_cast<void>(
      dispatcher_->Post(std::move(invocation).value(), context));
    return;
  }
  const auto result = registry_->Execute(invocation.value(), context);
  if (result.status != commands::ExecutionStatus::kOk) {
    LOG_F(ERROR, "panel command '{}' failed: {}", name, result.error);
  }
}

auto PanelCommands::RunBool(const std::string_view name, const bool value) const
  -> void
{
  Run(name, nlohmann::json { { kValue, value } });
}

auto PanelCommands::RunInt(
  const std::string_view name, const std::int64_t value) const -> void
{
  Run(name, nlohmann::json { { kValue, value } });
}

auto PanelCommands::RunFloat(
  const std::string_view name, const double value) const -> void
{
  Run(name, nlohmann::json { { kValue, value } });
}

auto PanelCommands::RunText(
  const std::string_view name, const std::string_view value) const -> void
{
  Run(name, nlohmann::json { { kValue, value } });
}

auto PanelCommands::RunColor(
  const std::string_view name, const graphics::Color& value) const -> void
{
  Run(name,
    nlohmann::json {
      { "r", value.r },
      { "g", value.g },
      { "b", value.b },
      { "a", value.a },
    });
}

auto PanelCommands::RunVec3(
  const std::string_view name, const glm::vec3& value) const -> void
{
  Run(name,
    nlohmann::json {
      { "x", value.x },
      { "y", value.y },
      { "z", value.z },
    });
}

auto PanelCommands::RunRgb(
  const std::string_view name, const glm::vec3& value) const -> void
{
  Run(name,
    nlohmann::json {
      { "r", value.x },
      { "g", value.y },
      { "b", value.z },
    });
}

} // namespace oxygen::examples
