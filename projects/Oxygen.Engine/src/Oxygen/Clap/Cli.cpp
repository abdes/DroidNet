//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

#include <fmt/format.h>
#include <fmt/printf.h>
#include <fmt/ranges.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Internal/Args.h>
#include <Oxygen/Clap/Parser/Parser.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/TextWrap/TextWrap.h>

using oxygen::clap::detail::Arguments;

namespace oxygen::clap {

namespace {

  constexpr unsigned int kFallbackOutputWidth = 80;

  auto TryParseColumnsEnv() -> std::optional<unsigned int>
  {
#if defined(_WIN32)
    char* columns = nullptr;
    size_t length = 0;
    if (_dupenv_s(&columns, &length, "COLUMNS") != 0 || columns == nullptr) {
      return std::nullopt;
    }
    const auto cleanup
      = std::unique_ptr<char, decltype(&std::free)>(columns, &std::free);
    if (*columns == '\0') {
      return std::nullopt;
    }
    char* end = nullptr;
    const auto value = std::strtoul(columns, &end, 10);
    if (end == columns || *end != '\0' || value < 1U) {
      return std::nullopt;
    }
    return static_cast<unsigned int>(value);
#else
    const char* columns = std::getenv("COLUMNS");
    if (columns == nullptr || *columns == '\0') {
      return std::nullopt;
    }
    char* end = nullptr;
    const auto value = std::strtoul(columns, &end, 10);
    if (end == columns || *end != '\0' || value < 1U) {
      return std::nullopt;
    }
    return static_cast<unsigned int>(value);
#endif
  }

  auto GetTerminalWidth() -> std::optional<unsigned int>
  {
#if defined(_WIN32)
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
      return TryParseColumnsEnv();
    }
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(handle, &info)) {
      return TryParseColumnsEnv();
    }
    const auto width
      = static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1);
    if (width < 1) {
      return TryParseColumnsEnv();
    }
    return static_cast<unsigned int>(width);
#else
    winsize size {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
      return static_cast<unsigned int>(size.ws_col);
    }
    return TryParseColumnsEnv();
#endif
  }

  auto ResolveOutputWidth(const std::optional<unsigned int>& configured,
    const bool auto_enabled) -> unsigned int
  {
    if (configured.has_value()) {
      return configured.value();
    }
    if (auto_enabled) {
      return GetTerminalWidth().value_or(kFallbackOutputWidth);
    }
    return kFallbackOutputWidth;
  }

} // namespace

CmdLineArgumentsError::~CmdLineArgumentsError() = default;

auto Cli::Parse(const int argc, const char** argv) -> CommandLineContext
{
  const Arguments cla { argc, argv };

  if (!program_name_) {
    program_name_ = cla.ProgramName();
  }

  const auto args_span = cla.Args();
  std::vector<std::string> args(args_span.begin(), args_span.end());

  // Simplify processing by transforming the shor or long option forms of
  // `version` and `help` into the corresponding unified command name.
  if (!args.empty()) {
    const std::string_view first = args[0];
    if (has_version_command_
      && (first == Command::VERSION_SHORT || first == Command::VERSION_LONG)) {
      args[0].assign(Command::VERSION);
    } else if (has_help_command_
      && (first == Command::HELP_SHORT || first == Command::HELP_LONG)) {
      args[0].assign(Command::HELP);
    }
  }

  const parser::Tokenizer tokenizer { args };
  const auto resolved_width
    = ResolveOutputWidth(output_width_, auto_output_width_);
  CommandLineContext context(
    ProgramName(), active_command_, ovm_, resolved_width);
  context.theme = theme_ ? theme_ : &CliTheme::Dark();
  context.global_option_groups = &global_option_groups_;
  parser::CmdLineParser parser(
    context, tokenizer, commands_, global_options_, global_option_groups_);
  if (parser.Parse()) {
    if (context.ovm.HasOption("theme")) {
      const auto& values = context.ovm.ValuesOf("theme");
      if (!values.empty()) {
        context.theme = &ResolveTheme(values.back().GetAs<CliThemeKind>());
      }
    }
    // Check if we need to handle a `version` or `help` command
    if (context.active_command->PathAsString() == "help"
      || context.ovm.HasOption("help")) {
      HandleHelpCommand(context);
    } else if (context.active_command->PathAsString() == "version") {
      HandleVersionCommand(context);
    }

    // Finalize values for all options across all commands so that StoreTo
    // side-effects are applied (and defaults propagated) once.
    for (const auto& cmd : commands_) {
      for (const auto& opt : cmd->CommandOptions()) {
        opt->FinalizeValue(context.ovm);
      }
      for (const auto& opt : cmd->PositionalArguments()) {
        opt->FinalizeValue(context.ovm);
      }
    }

    return context;
  }
  if (HasHelpCommand()) {
    context.out << fmt::format(
      "Try '{} --help' for more information.", program_name_.value())
                << '\n';
  }
  throw CmdLineArgumentsError(
    fmt::format("command line arguments parsing failed, try '{} --help' for "
                "more information.",
      program_name_.value()));
}

auto Cli::PrintDefaultCommand(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const auto default_command = std::ranges::find_if(
    commands_, [](const auto& command) { return command->IsDefault(); });
  if (default_command != commands_.end()) {
    (*default_command)->Print(context, width);
  }
}

auto Cli::PrintCommands(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  context.out << fmt::format(theme.section_header, "SUB-COMMANDS\n\n");
  for (const auto& command : commands_) {
    if (!command->IsDefault()) {
      context.out << "   "
                  << fmt::format(
                       theme.command_name, "{}", command->PathAsString())
                  << "\n";
      wrap::TextWrapper wrap = wrap::MakeWrapper()
                                 .Width(width)
                                 .IgnoreAnsiEscapeCodes()
                                 .TrimLines()
                                 .IndentWith()
                                 .Initially("     ")
                                 .Then("     ");
      context.out << wrap.Fill(command->About()).value_or("__wrapping_error__");
      context.out << "\n\n";
    }
  }
}

auto Cli::Print(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  PrintDefaultCommand(context, width);
  PrintCommands(context, width);
}

auto Cli::EnableVersionCommand() -> void
{
  const Command::Ptr command { CommandBuilder(Command::VERSION)
      .About(fmt::format(
        "Display version information.", ProgramName(), ProgramName())) };
  commands_.push_back(command);
  has_version_command_ = true;
}

auto Cli::HandleVersionCommand(const CommandLineContext& context) const -> void
{
  context.out << fmt::format(
    "{} version {}\n", program_name_.value_or("__no_program_name__"), version_)
              << '\n';
}

auto Cli::EnableHelpCommand() -> void
{
  const Command::Ptr command { CommandBuilder(Command::HELP)
      .About(fmt::format("Display detailed help information. `{} help` lists "
                         "available sub-commands and a summary of what they "
                         "do. See `{} help <command>` to get detailed help "
                         "for a specific sub-command.",
        ProgramName(), ProgramName()))
      .WithPositionalArguments(Option::Rest()
          .UserFriendlyName("SEGMENTS")
          .About("The path segments (in the correct order) of the "
                 "sub-command for which help information should be "
                 "displayed.")
          .WithValue<std::string>()
          .Build()) };
  commands_.push_back(command);
  has_help_command_ = true;
}
auto Cli::HandleHelpCommand(const CommandLineContext& context) const -> void
{
  if (context.ovm.HasOption("help")) {
    context.active_command->Print(context, context.output_width);
  } else if (context.active_command->PathAsString() == "help") {
    if (context.ovm.HasOption(Option::key_rest_)) {
      const auto& values = context.ovm.ValuesOf(Option::key_rest_);
      DCHECK_F(!values.empty());
      std::vector<std::string> command_path;
      command_path.reserve(values.size());
      for (const auto& value : values) {
        command_path.push_back(value.GetAs<std::string>());
      }

      auto command = std::ranges::find_if(
        commands_, [&command_path](const Command::Ptr& cmd) {
          return cmd->Path() == command_path;
        });
      if (command != commands_.end()) {
        (*command)->Print(context, context.output_width);
      } else {
        context.err << fmt::format(
          "The path `{}` does not correspond to a known command.\n",
          fmt::join(command_path, " "));
        context.out << fmt::format("Try '{} --help' for more information.",
          program_name_.value_or("__no_program_name__"))
                    << '\n';
      }
    } else {
      PrintDefaultCommand(context, context.output_width);
      PrintCommands(context, context.output_width);
    }
  }
}

auto Cli::EnableThemeSelectionOption() -> void
{
  if (has_theme_selection_option_) {
    return;
  }
  has_theme_selection_option_ = true;
  WithGlobalOption(Option::WithKey("theme")
      .Long("theme")
      .About("Select output theme: dark, light, plain.")
      .WithValue<CliThemeKind>()
      .Build());
}
} // namespace oxygen::clap
