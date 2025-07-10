// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#include <numeric>

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

auto CommandDiagnostic(const oxygen::clap::parser::detail::CommandPtr& command)
  -> std::string
{
  if (!command || command->IsDefault()) {
    return "";
  }
  return fmt::format("while parsing command '{}',", command->PathAsString());
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

  auto description
    = fmt::format("{} option '{}' seen as '{}',"
                  " got value token '{}' which failed to parse to type {},"
                  " and the option has no implicit value",
      CommandDiagnostic(context->active_command), context->active_option->Key(),
      context->active_option_flag, token, "<TODO: TYPE NAME>");
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
  AppendOptionalMessage(description, message);
  return description;
}
