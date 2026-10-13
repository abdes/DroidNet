//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Params.h>

namespace oxygen::commands {

namespace {

  using nlohmann::json;

  auto Expected(const ParamType type) -> std::string_view
  {
    switch (type) {
    case ParamType::kBool:
      return "a boolean";
    case ParamType::kInt:
      return "an integer";
    case ParamType::kDouble:
      return "a number";
    case ParamType::kString:
    case ParamType::kPath:
      return "a string";
    case ParamType::kEnum:
      return "one of the allowed values";
    case ParamType::kScalar:
      return "a boolean, number or string";
    }
    return "a value";
  }

  template <typename T>
  auto ParseNumber(const std::string_view text) -> std::optional<T>
  {
    auto value = T {};
    const auto* const end = std::to_address(text.end());
    const auto parsed
      = std::from_chars(std::to_address(text.begin()), end, value);
    if (parsed.ec != std::errc {} || parsed.ptr != end || text.empty()) {
      return std::nullopt;
    }
    return value;
  }

  auto ParseBool(const std::string_view text) -> std::optional<bool>
  {
    if (text == "true" || text == "1" || text == "on") {
      return true;
    }
    if (text == "false" || text == "0" || text == "off") {
      return false;
    }
    return std::nullopt;
  }

  //! Range and allowed-value checks shared by text and JSON binding.
  auto Check(const CommandParam& param, const ParamValue& value)
    -> std::optional<std::string>
  {
    auto number = std::optional<double> {};
    if (const auto* integer = std::get_if<int64_t>(&value)) {
      number = static_cast<double>(*integer);
    } else if (const auto* real = std::get_if<double>(&value)) {
      number = *real;
    }
    if (number && param.min_value && *number < *param.min_value) {
      return fmt::format("{}: {} is below the minimum {}", param.name, *number,
        *param.min_value);
    }
    if (number && param.max_value && *number > *param.max_value) {
      return fmt::format("{}: {} is above the maximum {}", param.name, *number,
        *param.max_value);
    }
    if (param.type == ParamType::kEnum) {
      const auto& text = std::get<std::string>(value);
      if (!std::ranges::contains(param.allowed_values, text)) {
        return fmt::format("{}: '{}' is not one of: {}", param.name, text,
          fmt::join(param.allowed_values, ", "));
      }
    }
    return std::nullopt;
  }

  auto FromText(const CommandParam& param, const std::string& text)
    -> Result<ParamValue, std::string>
  {
    const auto fail = [&]() -> Result<ParamValue, std::string> {
      return Err(fmt::format(
        "{}: expected {}, got '{}'", param.name, Expected(param.type), text));
    };
    switch (param.type) {
    case ParamType::kBool:
      if (const auto value = ParseBool(text)) {
        return Ok(ParamValue { *value });
      }
      return fail();
    case ParamType::kInt:
      if (const auto value = ParseNumber<int64_t>(text)) {
        return Ok(ParamValue { *value });
      }
      return fail();
    case ParamType::kDouble:
      if (const auto value = ParseNumber<double>(text)) {
        return Ok(ParamValue { *value });
      }
      return fail();
    case ParamType::kString:
    case ParamType::kPath:
    case ParamType::kEnum:
    case ParamType::kScalar:
      return Ok(ParamValue { text });
    }
    return fail();
  }

  auto FromJson(const CommandParam& param, const json& value)
    -> Result<ParamValue, std::string>
  {
    const auto fail = [&]() -> Result<ParamValue, std::string> {
      return Err(fmt::format("{}: expected {}, got {}", param.name,
        Expected(param.type), value.dump()));
    };
    switch (param.type) {
    case ParamType::kBool:
      return value.is_boolean() ? Ok(ParamValue { value.get<bool>() }) : fail();
    case ParamType::kInt:
      return value.is_number_integer() ? Ok(ParamValue { value.get<int64_t>() })
                                       : fail();
    case ParamType::kDouble:
      return value.is_number() ? Ok(ParamValue { value.get<double>() })
                               : fail();
    case ParamType::kString:
    case ParamType::kPath:
    case ParamType::kEnum:
      return value.is_string() ? Ok(ParamValue { value.get<std::string>() })
                               : fail();
    case ParamType::kScalar:
      if (value.is_boolean()) {
        return Ok(ParamValue { value.get<bool>() });
      }
      if (value.is_number_integer()) {
        return Ok(ParamValue { value.get<int64_t>() });
      }
      if (value.is_number()) {
        return Ok(ParamValue { value.get<double>() });
      }
      if (value.is_string()) {
        return Ok(ParamValue { value.get<std::string>() });
      }
      return fail();
    }
    return fail();
  }

  auto FindParam(const std::span<const CommandParam> params,
    const std::string_view name) -> const CommandParam*
  {
    const auto it = std::ranges::find(params, name, &CommandParam::name);
    return it == params.end() ? nullptr : std::to_address(it);
  }

  //! Arguments bound so far, the problems found, and the parameters given
  //! (valid or not), so a bad value is not also reported as missing.
  struct Binding {
    CommandArgs args;
    std::vector<std::string> errors;
    std::vector<std::string_view> given;
  };

  //! Converts, checks and stores one value, or records why not.
  template <typename Convert>
  auto Accept(const CommandParam& param, Convert&& convert, Binding& binding)
    -> void
  {
    binding.given.emplace_back(param.name);
    auto value = std::forward<Convert>(convert)();
    if (!value) {
      binding.errors.push_back(std::move(value).error());
      return;
    }
    if (auto problem = Check(param, value.value())) {
      binding.errors.push_back(std::move(*problem));
      return;
    }
    binding.args.Set(param.name, std::move(value).value());
  }

  //! Applies defaults and reports missing required parameters.
  auto Complete(const std::span<const CommandParam> params, Binding& binding)
    -> void
  {
    for (const auto& param : params) {
      if (std::ranges::contains(binding.given, param.name)) {
        continue;
      }
      if (param.default_value) {
        binding.args.Set(param.name, *param.default_value);
      } else if (param.required) {
        binding.errors.push_back(
          fmt::format("{}: missing required argument", param.name));
      }
    }
  }

  auto Finish(Binding binding) -> Result<CommandArgs, ArgumentErrors>
  {
    if (!binding.errors.empty()) {
      return Err(ArgumentErrors { .messages = std::move(binding.errors) });
    }
    return Ok(std::move(binding.args));
  }

  auto SchemaType(const ParamType type) -> json
  {
    switch (type) {
    case ParamType::kBool:
      return "boolean";
    case ParamType::kInt:
      return "integer";
    case ParamType::kDouble:
      return "number";
    case ParamType::kString:
    case ParamType::kPath:
    case ParamType::kEnum:
      return "string";
    case ParamType::kScalar:
      return json::array({ "boolean", "integer", "number", "string" });
    }
    return "string";
  }

  auto ToJsonValue(const ParamValue& value) -> json
  {
    return std::visit([](const auto& v) -> json { return v; }, value);
  }

} // namespace

auto CommandArgs::Has(const std::string_view name) const noexcept -> bool
{
  return Find(name) != nullptr;
}

auto CommandArgs::Find(const std::string_view name) const noexcept
  -> observer_ptr<const ParamValue>
{
  for (const auto& [key, value] : values_) {
    if (key == name) {
      return observer_ptr { &value };
    }
  }
  return nullptr;
}

auto CommandArgs::GetBool(const std::string_view name) const -> bool
{
  const auto value = Find(name);
  CHECK_F(value != nullptr && std::holds_alternative<bool>(*value),
    "argument '{}' is not a present boolean", name);
  return std::get<bool>(*value);
}

auto CommandArgs::GetInt(const std::string_view name) const -> int64_t
{
  const auto value = Find(name);
  CHECK_F(value != nullptr && std::holds_alternative<int64_t>(*value),
    "argument '{}' is not a present integer", name);
  return std::get<int64_t>(*value);
}

auto CommandArgs::GetDouble(const std::string_view name) const -> double
{
  const auto value = Find(name);
  CHECK_F(value != nullptr && std::holds_alternative<double>(*value),
    "argument '{}' is not a present number", name);
  return std::get<double>(*value);
}

auto CommandArgs::GetString(const std::string_view name) const
  -> const std::string&
{
  const auto value = Find(name);
  CHECK_F(value != nullptr && std::holds_alternative<std::string>(*value),
    "argument '{}' is not a present string", name);
  return std::get<std::string>(*value);
}

auto CommandArgs::Set(std::string name, ParamValue value) -> void
{
  for (auto& [key, existing] : values_) {
    if (key == name) {
      existing = std::move(value);
      return;
    }
  }
  values_.emplace_back(std::move(name), std::move(value));
}

auto CommandArgs::ToJson() const -> nlohmann::json
{
  auto object = json::object();
  for (const auto& [key, value] : values_) {
    object[key] = ToJsonValue(value);
  }
  return object;
}

auto BindText(const std::span<const CommandParam> params,
  const std::span<const std::string> tokens)
  -> Result<CommandArgs, ArgumentErrors>
{
  auto binding = Binding {};
  auto next_positional = std::size_t { 0U };
  auto rest = std::vector<std::string> {};
  const CommandParam* rest_param = nullptr;

  for (const auto& token : tokens) {
    if (rest_param != nullptr) {
      rest.push_back(token);
      continue;
    }
    if (const auto equals = token.find('='); equals != std::string::npos) {
      const auto name = std::string_view(token).substr(0U, equals);
      if (const auto* param = FindParam(params, name)) {
        const auto text = token.substr(equals + 1U);
        Accept(
          *param,
          [&] -> Result<ParamValue, std::string> {
            return FromText(*param, text);
          },
          binding);
        continue;
      }
    }
    while (next_positional < params.size()
      && std::ranges::contains(binding.given, params[next_positional].name)) {
      ++next_positional;
    }
    if (next_positional >= params.size()) {
      binding.errors.push_back(fmt::format("unexpected argument '{}'", token));
      continue;
    }
    const auto& param = params[next_positional++];
    if (param.rest_of_line) {
      rest_param = &param;
      rest.push_back(token);
      continue;
    }
    Accept(
      param,
      [&] -> Result<ParamValue, std::string> { return FromText(param, token); },
      binding);
  }
  if (rest_param != nullptr) {
    auto joined = fmt::format("{}", fmt::join(rest, " "));
    Accept(
      *rest_param,
      [&] -> Result<ParamValue, std::string> {
        return FromText(*rest_param, joined);
      },
      binding);
  }
  Complete(params, binding);
  return Finish(std::move(binding));
}

auto BindJson(const std::span<const CommandParam> params,
  const nlohmann::json& arguments) -> Result<CommandArgs, ArgumentErrors>
{
  auto binding = Binding {};
  if (!arguments.is_null() && !arguments.is_object()) {
    binding.errors.emplace_back("arguments must be an object");
    return Finish(std::move(binding));
  }
  if (arguments.is_object()) {
    for (const auto& [name, value] : arguments.items()) {
      const auto* param = FindParam(params, name);
      if (param == nullptr) {
        binding.errors.push_back(fmt::format("unexpected argument '{}'", name));
        continue;
      }
      Accept(
        *param,
        [&] -> Result<ParamValue, std::string> {
          return FromJson(*param, value);
        },
        binding);
    }
  }
  Complete(params, binding);
  return Finish(std::move(binding));
}

auto ParamsSchema(const std::span<const CommandParam> params) -> nlohmann::json
{
  auto properties = json::object();
  auto required = json::array();
  for (const auto& param : params) {
    auto property = json::object();
    property["type"] = SchemaType(param.type);
    if (!param.help.empty()) {
      property["description"] = param.help;
    }
    if (param.default_value) {
      property["default"] = ToJsonValue(*param.default_value);
    }
    if (param.min_value) {
      property["minimum"] = *param.min_value;
    }
    if (param.max_value) {
      property["maximum"] = *param.max_value;
    }
    if (param.type == ParamType::kEnum) {
      property["enum"] = param.allowed_values;
    }
    properties[param.name] = std::move(property);
    if (param.required && !param.default_value) {
      required.push_back(param.name);
    }
  }
  auto schema = json {
    { "type", "object" },
    { "properties", std::move(properties) },
    { "additionalProperties", false },
  };
  if (!required.empty()) {
    schema["required"] = std::move(required);
  }
  return schema;
}

auto to_string(const ParamType value) -> const char*
{
  switch (value) {
  case ParamType::kBool:
    return "bool";
  case ParamType::kInt:
    return "int";
  case ParamType::kDouble:
    return "double";
  case ParamType::kString:
    return "string";
  case ParamType::kEnum:
    return "enum";
  case ParamType::kPath:
    return "path";
  case ParamType::kScalar:
    return "scalar";
  }
  return "__NotSupported__";
}

} // namespace oxygen::commands
