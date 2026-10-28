//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/color.h>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/TextWrap/TextWrap.h>

#ifdef _WIN32
#  include <windows.h> // IWYU pragma: keep
#else
#  include <cstdlib>

#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Internal/Args.h>
#include <Oxygen/Clap/Internal/StyledWrap.h>
#include <Oxygen/Clap/Parser/Parser.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>

using oxygen::clap::detail::Arguments;

namespace oxygen::clap {

namespace {

  constexpr unsigned int kFallbackOutputWidth = 80;

  //! The value of the environment variable `name`, if it is set.
  auto ReadEnvironment(const char* name) -> std::optional<std::string>
  {
#ifdef _WIN32
    // The MSVC runtime deprecates `std::getenv`; the Win32 call is the
    // supported way to read the environment.
    const auto size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) {
      return std::nullopt;
    }
    auto value = std::string(size, '\0');
    const auto written = GetEnvironmentVariableA(name, value.data(), size);
    if (written == 0 || written >= size) {
      return std::nullopt;
    }
    value.resize(written);
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
      return std::nullopt;
    }
    return std::string(value);
#endif
  }

  //! The terminal width the `COLUMNS` environment variable states, if it
  //! states a positive number.
  auto TryParseColumnsEnv() -> std::optional<unsigned int>
  {
    const auto columns = ReadEnvironment("COLUMNS");
    if (!columns.has_value()) {
      return std::nullopt;
    }
    const auto* const first = std::to_address(columns->begin());
    const auto* const last = std::to_address(columns->end());
    auto value = 0U;
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc {} || end != last || value < 1U) {
      return std::nullopt;
    }
    return value;
  }

  auto GetTerminalWidth() -> std::optional<unsigned int>
  {
#ifdef _WIN32
    // `HANDLE` is a pointer; spelling it out keeps the handle itself constant,
    // not what it points to.
    auto* const handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
      return TryParseColumnsEnv();
    }
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(handle, &info) == FALSE) {
      return TryParseColumnsEnv();
    }
    const auto width = info.srWindow.Right - info.srWindow.Left + 1;
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

auto Cli::Parse(const std::span<const char* const> command_line,
  const CliStreams streams) -> CommandLineContext
{
  const Arguments cla { command_line };

  if (!program_name_) {
    program_name_ = cla.ProgramName();
  }

  const auto args_span = cla.Args();
  std::vector<std::string> args(args_span.begin(), args_span.end());

  // Simplify processing by transforming the short or long option forms of
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

    // Support the common usage: `tool subcommand --help` -> `tool help
    // subcommand`. If a help token is present anywhere after the first token,
    // rewrite the arguments so that we run the help command for the target
    // command path.
    if (has_help_command_) {
      for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == Command::HELP_LONG || args[i] == Command::HELP_SHORT) {
          std::vector<std::string> rewritten;
          rewritten.reserve(i + 1);
          rewritten.emplace_back(Command::HELP);
          for (size_t j = 0; j < i; ++j) {
            rewritten.emplace_back(std::move(args[j]));
          }
          args = std::move(rewritten);
          break;
        }
      }
    }
  }

  const parser::Tokenizer tokenizer { args };
  const auto resolved_width
    = ResolveOutputWidth(output_width_, auto_output_width_);
  // Each parse fills a context of its own, so values, including defaults, from
  // a previous parse cannot leak into it.
  CommandLineContext context(ProgramName(), nullptr, resolved_width, streams);
  context.theme = theme_ ? theme_ : &CliTheme::Dark();
  context.global_option_groups = &global_option_groups_;
  parser::CmdLineParser parser(context, tokenizer, commands_, global_options_);
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

    // Finalize values for all options across all commands, and for the global
    // options, so that StoreTo side-effects are applied (and defaults
    // propagated) once.
    for (const auto& cmd : commands_) {
      for (const auto& opt : cmd->CommandOptions()) {
        opt->FinalizeValue(context.ovm);
      }
      for (const auto& opt : cmd->PositionalArguments()) {
        opt->FinalizeValue(context.ovm);
      }
    }
    for (const auto& opt : global_options_) {
      opt->FinalizeValue(context.ovm);
    }

    return context;
  }
  // The hint belongs with the error the parser has just reported.
  if (HasHelpCommand()) {
    *context.err << fmt::format(
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
  const auto default_command = std::ranges::find_if(commands_,
    [](const auto& command) -> auto { return command->IsDefault(); });
  if (default_command != commands_.end()) {
    (*default_command)->Print(context, width);
  }
}

auto Cli::PrintCommands(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  *context.out << fmt::format(theme.section_header, "SUB-COMMANDS\n\n");
  for (const auto& command : commands_) {
    if (!command->IsDefault()) {
      *context.out << "   "
                   << fmt::format(
                        theme.command_name, "{}", command->PathAsString())
                   << "\n";
      wrap::TextWrapper wrap
        = detail::MakeStyledWrapper(width, "     ", "     ");
      *context.out
        << wrap.Fill(command->About()).value_or("__wrapping_error__");
      *context.out << "\n\n";
    }
  }
}

auto Cli::PrintFooter(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  if (footer_.empty()) {
    return;
  }
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  *context.out << fmt::format(theme.section_header, "FOOTER\n");
  const wrap::TextWrapper wrap = detail::MakeStyledWrapper(width, "   ", "   ");
  *context.out << wrap.Fill(footer_).value_or("__wrapping_error__");
  *context.out << "\n\n";
}

auto Cli::Print(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  PrintDefaultCommand(context, width);
  PrintCommands(context, width);
  PrintFooter(context, width);
}

auto Cli::EnableVersionCommand() -> void
{
  const Command::Ptr command {
    CommandBuilder(Command::VERSION)
      .About(fmt::format(
        "Display version information.", ProgramName(), ProgramName())),
  };
  commands_.push_back(command);
  has_version_command_ = true;
}

auto Cli::HandleVersionCommand(const CommandLineContext& context) const -> void
{
  *context.out << fmt::format(
    "{} version {}\n", program_name_.value_or("__no_program_name__"), version_)
               << '\n';
}

auto Cli::EnableHelpCommand() -> void
{
  const Command::Ptr command {
    CommandBuilder(Command::HELP)
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
          .Build()),
  };
  commands_.push_back(command);
  has_help_command_ = true;
}
auto Cli::HandleHelpCommand(const CommandLineContext& context) const -> void
{
  if (context.ovm.HasOption("help")) {
    context.active_command->Print(context, context.output_width);
    PrintFooter(context, context.output_width);
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
        commands_, [&command_path](const Command::Ptr& cmd) -> bool {
          return cmd->Path() == command_path;
        });
      if (command != commands_.end()) {
        (*command)->Print(context, context.output_width);
        PrintFooter(context, context.output_width);
      } else {
        *context.err << fmt::format(
          "The path `{}` does not correspond to a known command.\n",
          fmt::join(command_path, " "));
        *context.err << fmt::format("Try '{} --help' for more information.",
          program_name_.value_or("__no_program_name__"))
                     << '\n';
      }
    } else {
      PrintDefaultCommand(context, context.output_width);
      PrintCommands(context, context.output_width);
      PrintFooter(context, context.output_width);
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
