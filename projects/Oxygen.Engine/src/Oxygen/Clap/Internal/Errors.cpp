// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#include <algorithm>
#include <cctype>
#include <numeric>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Internal/Errors.h>

namespace {
void AppendOptionalMessage(std::string& description, const char* message)
{
  if (message == nullptr || strlen(message) == 0) {
    description.append(".");
    return;
  }
  description.append(" - ").append(message).append(".");
}

auto ToLower(std::string_view text) -> std::string
{
  std::string lowered(text.begin(), text.end());
  std::ranges::transform(lowered, lowered.begin(),
    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

auto LevenshteinDistance(std::string_view left, std::string_view right)
  -> std::size_t
{
  if (left.empty()) {
    return right.size();
  }
  if (right.empty()) {
    return left.size();
  }
  std::vector<std::size_t> costs(right.size() + 1U, 0U);
  for (std::size_t j = 0; j <= right.size(); ++j) {
    costs[j] = j;
  }
  for (std::size_t i = 1; i <= left.size(); ++i) {
    std::size_t prev = costs[0];
    costs[0] = i;
    for (std::size_t j = 1; j <= right.size(); ++j) {
      const std::size_t temp = costs[j];
      const std::size_t cost = left[i - 1] == right[j - 1] ? 0U : 1U;
      costs[j] = std::min({ costs[j] + 1U, costs[j - 1] + 1U, prev + cost });
      prev = temp;
    }
  }
  return costs.back();
}

auto BestMatches(std::string_view input,
  const std::vector<std::pair<std::string, std::string>>& candidates,
  const std::optional<std::size_t> max_distance_override = std::nullopt)
  -> std::vector<std::string>
{
  if (input.empty()) {
    return {};
  }
  const auto lowered_input = ToLower(input);
  const auto max_distance = max_distance_override.value_or(input.size() <= 4U
      ? 2U
      : std::min<std::size_t>(
          3U, std::max<std::size_t>(1U, input.size() / 2U)));

  std::vector<std::pair<std::string, std::size_t>> ranked;
  ranked.reserve(candidates.size());
  for (const auto& [display, compare] : candidates) {
    const auto distance = LevenshteinDistance(lowered_input, ToLower(compare));
    if (distance <= max_distance) {
      ranked.emplace_back(display, distance);
    }
  }
  std::ranges::sort(ranked, {}, &std::pair<std::string, std::size_t>::second);

  std::vector<std::string> results;
  for (const auto& [display, distance] : ranked) {
    if (results.size() >= 3U) {
      break;
    }
    results.push_back("'" + display + "'");
  }
  return results;
}

auto CommandDiagnostic(const oxygen::clap::parser::detail::CommandPtr& command)
  -> std::string
{
  if (!command || command->IsDefault()) {
    return "";
  }
  return fmt::format("while parsing command '{}',", command->PathAsString());
}

auto CommandSuggestions(
  const oxygen::clap::parser::detail::ParserContextPtr& context,
  const std::vector<std::string>& path_segments) -> std::vector<std::string>
{
  if (!context || path_segments.empty()) {
    return {};
  }
  std::vector<std::pair<std::string, std::string>> candidates;
  candidates.reserve(context->commands.size());
  for (const auto& command : context->commands) {
    if (!command) {
      continue;
    }
    const auto name = command->PathAsString();
    if (!name.empty()) {
      candidates.emplace_back(name, name);
    }
  }
  return BestMatches(path_segments.back(), candidates, 5U);
}

auto OptionSuggestions(
  const oxygen::clap::parser::detail::ParserContextPtr& context,
  const std::string& token) -> std::vector<std::string>
{
  if (!context) {
    return {};
  }
  std::vector<std::pair<std::string, std::string>> candidates;
  const auto add_option
    = [&candidates](const oxygen::clap::parser::detail::OptionPtr& option) {
        if (!option) {
          return;
        }
        if (!option->Short().empty()) {
          const auto display = "-" + option->Short();
          candidates.emplace_back(display, option->Short());
        }
        if (!option->Long().empty()) {
          const auto display = "--" + option->Long();
          candidates.emplace_back(display, option->Long());
        }
      };

  if (context->active_command) {
    for (const auto& option : context->active_command->CommandOptions()) {
      add_option(option);
    }
  }
  for (const auto& option : context->global_options) {
    add_option(option);
  }

  return BestMatches(token, candidates);
}

} // namespace

auto oxygen::clap::parser::detail::UnrecognizedCommand(
  const std::vector<std::string>& path_segments, const char* message)
  -> std::string
{
  auto description = fmt::format(
    "Unrecognized command with path '{}'", fmt::join(path_segments, " "));
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::UnrecognizedCommand(
  const ParserContextPtr& context,
  const std::vector<std::string>& path_segments, const char* message)
  -> std::string
{
  auto description = fmt::format(
    "Unrecognized command with path '{}'", fmt::join(path_segments, " "));
  const auto suggestions = CommandSuggestions(context, path_segments);
  if (!suggestions.empty()) {
    description.append(" Did you mean ")
      .append(fmt::format("{}?", fmt::join(suggestions, ", ")));
  }
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::MissingCommand(
  const ParserContextPtr& context, const char* message) -> std::string
{
  auto supported_commands
    = std::accumulate(context->commands.cbegin(), context->commands.cend(),
      std::vector<std::string>(), [](auto dest, const auto& command) {
        dest.push_back("'" + command->PathAsString() + "'");
        return dest;
      });

  auto description
    = fmt::format("You must specify a command. Supported commands are: {}",
      fmt::join(supported_commands, ", "));
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::UnrecognizedOption(
  const ParserContextPtr& context, const std::string& token,
  const char* message) -> std::string
{
  auto option_name = (token.length() == 1) ? "-" + token : "--" + token;
  auto description = fmt::format("{} '{}' is not a recognized option",
    CommandDiagnostic(context->active_command), option_name);
  const auto suggestions = OptionSuggestions(context, token);
  if (!suggestions.empty()) {
    description.append(" Did you mean ")
      .append(fmt::format("{}?", fmt::join(suggestions, ", ")));
  }
  AppendOptionalMessage(description, message);
  return description;
}
auto oxygen::clap::parser::detail::IllegalMultipleOccurrence(
  const ParserContextPtr& context, const char* message) -> std::string
{
  DCHECK_NOTNULL_F(context->active_option);
  DCHECK_GT_F(context->ovm.OccurrencesOf(context->active_option->Key()), 0UL);

  const auto& option_name = context->active_option->Key();
  auto description
    = fmt::format("{} new occurrence for option '{}' "
                  "as '{}' is illegal; it can only be used one time and it "
                  "appeared before with value '{}'",
      CommandDiagnostic(context->active_command), option_name,
      context->active_option_flag,
      context->ovm.ValuesOf(option_name).front().OriginalToken());
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::OptionSyntaxError(
  const ParserContextPtr& context, const char* message) -> std::string
{
  auto description = fmt::format("{} option '{}' is using an invalid syntax",
    CommandDiagnostic(context->active_command), context->active_option->Key());
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::MissingValueForOption(
  const ParserContextPtr& context, const char* message) -> std::string
{
  auto description
    = fmt::format("{} option '{}' seen as '{}' "
                  "has no value on the command line and no implicit one",
      CommandDiagnostic(context->active_command), context->active_option->Key(),
      context->active_option_flag);
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::InvalidValueForOption(
  const ParserContextPtr& context, const std::string& token,
  const char* message) -> std::string
{
  return InvalidValueForOption(context, context->active_option, token, message);
}

auto oxygen::clap::parser::detail::InvalidValueForOption(
  const ParserContextPtr& context, const OptionPtr& option,
  const std::string& token, const char* message) -> std::string
{
  std::string option_name = "<positional>";
  std::string option_flag = "<positional>";
  std::string expected_type;
  if (option) {
    option_name = option->Key();
    option_flag = context->active_option_flag;
    if (const auto semantics = option->value_semantic()) {
      expected_type = semantics->ExpectedTypeName();
    }
  }
  const auto type_clause = expected_type.empty()
    ? "the expected type"
    : fmt::format("expected type '{}'", expected_type);
  auto description
    = fmt::format("{} option '{}' seen as '{}',"
                  " got value token '{}' which failed to parse to {},"
                  " and the option has no implicit value",
      CommandDiagnostic(context->active_command), option_name, option_flag,
      token, type_clause);
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::MissingRequiredOption(
  const CommandPtr& command, const OptionPtr& option, const char* message)
  -> std::string
{
  auto description
    = fmt::format("{} no {} '{}' was specified. "
                  "It is required and does not have a default value",
      CommandDiagnostic(command),
      (option->IsPositional() ? "positional argument" : "option"),
      option->UserFriendlyName());
  AppendOptionalMessage(description, message);
  return description;
}

auto oxygen::clap::parser::detail::UnexpectedPositionalArguments(
  const ParserContextPtr& context, const char* message) -> std::string
{
  auto description = fmt::format("{} argument{} '{}' "
                                 "{} not expected by any option",
    CommandDiagnostic(context->active_command),
    context->positional_tokens.size() > 1 ? "s" : "",
    fmt::join(context->positional_tokens, ", "),
    context->positional_tokens.size() > 1 ? "are" : "is");
  const auto suggestions
    = CommandSuggestions(context, context->positional_tokens);
  if (!suggestions.empty()) {
    description.append(" Did you mean ")
      .append(fmt::format("{}?", fmt::join(suggestions, ", ")));
  }
  AppendOptionalMessage(description, message);
  return description;
}
auto oxygen::clap::parser::detail::PositionalAfterRestError(
  const ParserContextPtr& context, const OptionPtr& rest_option,
  const char* message) -> std::string
{
  std::vector<std::string> after_rest_names;
  const auto& positionals = context->active_command->PositionalArguments();
  bool seen_rest = false;
  for (const auto& opt : positionals) {
    if (seen_rest) {
      after_rest_names.push_back(opt->UserFriendlyName());
    }
    if (opt == rest_option) {
      seen_rest = true;
    }
  }
  std::string after_rest_str;
  if (!after_rest_names.empty()) {
    after_rest_str = fmt::format("{}", fmt::join(after_rest_names, ", "));
  }
  auto description = fmt::format(
    "Invalid command definition: positional argument(s){}{} defined after rest "
    "positional '{}' ({}). This is not allowed",
    after_rest_names.empty() ? "" : " ", after_rest_str,
    rest_option ? rest_option->UserFriendlyName() : "<rest>",
    rest_option ? rest_option->Key() : "<rest>");
  AppendOptionalMessage(description, message);
  return description;
}
