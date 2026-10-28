//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/CookedVersions.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::toolkit {

namespace {

  constexpr std::string_view kVersionsCommand = "versions";

  //! Prints the cooked format versions the tool reads.
  class VersionsCommand final : public ToolCommand {
  public:
    explicit VersionsCommand(const ToolInfo& info)
      : info_(info)
    {
    }

    [[nodiscard]] auto Name() const -> std::string_view override
    {
      return kVersionsCommand;
    }

    [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override
    {
      return clap::CommandBuilder(std::string(kVersionsCommand))
        .About(info_.cooks_content
            ? "Print the cooked format versions and the importer revisions"
            : "Print the versions of the cooked formats this tool reads.")
        .WithOption(clap::Option::WithKey("json")
            .About("Print one JSON object")
            .Long("json")
            .WithValue<bool>()
            .DefaultValue(false)
            .StoreTo(&json_)
            .Build());
    }

    [[nodiscard]] auto Run(ToolContext& context) -> ToolResult override
    {
      PrintCookedVersions(context.Out(),
        CookedVersionsReport {
          .program = info_.program,
          .version = info_.version,
          .importers = info_.cooks_content,
          .json = json_,
        });
      return {};
    }

  private:
    ToolInfo info_;
    bool json_ = false;
  };

  auto Report(std::ostream& err, const std::string_view message) -> void
  {
    if (!message.empty()) {
      err << "ERROR: " << message << '\n';
    }
  }

  //! The arguments, space separated.
  auto CommandLineToString(const std::span<const char* const> args)
    -> std::string
  {
    return args
      | std::views::transform(
        [](const char* arg) -> std::string_view { return arg; })
      | std::views::join_with(' ') | std::ranges::to<std::string>();
  }

} // namespace

ToolApp::ToolApp(
  const ToolInfo& info, const std::span<ToolCommand* const> commands)
  : info_(info)
  , versions_(std::make_unique<VersionsCommand>(info))
  , commands_(commands.begin(), commands.end())
{
  commands_.push_back(versions_.get());

  clap::CliBuilder builder;
  builder.ProgramName(std::string(info.program))
    .Version(std::string(info.version))
    .About(std::string(info.about))
    .WithHelpCommand()
    .WithVersionCommand();
  for (const auto& group : info.global_options) {
    builder.WithGlobalOptions(group);
  }
  for (auto* command : commands_) {
    auto built = command->BuildCommand();
    // `Run` selects a command by the path the parser reports, so a command
    // must register itself under its own name.
    CHECK_F(built->PathAsString() == command->Name(),
      "Command '{}' registers itself as '{}'", command->Name(),
      built->PathAsString());
    builder.WithCommand(std::move(built));
  }
  cli_ = builder.Build();
}

ToolApp::~ToolApp() = default;

auto ExitCodeOf(std::ostream& err, const std::function<ToolResult()>& body)
  -> int
{
  try {
    if (const auto result = body(); !result) {
      Report(err, result.error().message);
      return result.error().code;
    }
    return static_cast<int>(ExitCode::kOk);
  } catch (const std::exception& ex) {
    Report(err, ex.what());
  } catch (...) {
    Report(err, "unknown exception");
  }
  return static_cast<int>(ExitCode::kFailed);
}

namespace {

  //! Runs `command` between the tool's `Begin` and `End`, if it has any.
  auto RunWithLifecycle(const observer_ptr<ToolLifecycle> lifecycle,
    const clap::CommandLineContext& parsed, ToolCommand& command,
    ToolContext& context) -> int
  {
    if (lifecycle == nullptr) {
      return ExitCodeOf(context.Err(), [&] { return command.Run(context); });
    }

    auto begun = false;
    auto code = ExitCodeOf(context.Err(), [&] -> ToolResult {
      if (auto begin = lifecycle->Begin(parsed, command, context); !begin) {
        return begin;
      }
      begun = true;
      return command.Run(context);
    });
    if (begun) {
      lifecycle->End();
    }
    return code;
  }

} // namespace

auto ToolApp::Run(
  const std::span<const char* const> args, const ToolStreams streams) -> int
{
  try {
    const auto parsed = cli_->Parse(
      args, clap::CliStreams { .out = streams.out, .err = streams.err });
    const auto path = parsed.active_command->PathAsString();
    if (path == clap::Command::VERSION || path == clap::Command::HELP
      || parsed.ovm.HasOption(clap::Command::HELP)) {
      return static_cast<int>(ExitCode::kOk);
    }

    // The parser accepts only registered commands, and each one registered
    // itself under its name.
    const auto command = std::ranges::find(commands_, path, &ToolCommand::Name);
    CHECK_F(command != commands_.end(), "No command is named '{}'", path);
    ToolContext context(streams, parsed.ovm, CommandLineToString(args));
    return RunWithLifecycle(info_.lifecycle, parsed, **command, context);
  } catch (const clap::CmdLineArgumentsError& ex) {
    // The parser has already printed its account of the error.
    if (info_.on_parse_error != nullptr) {
      return info_.on_parse_error(ex, streams);
    }
    return static_cast<int>(ExitCode::kUsage);
  } catch (const std::exception& ex) {
    // A command reports its own failures, so this one came from parsing.
    Report(*streams.err, ex.what());
    return static_cast<int>(ExitCode::kFailed);
  }
}

auto RunTool(const ToolInfo& info, const std::span<ToolCommand* const> commands,
  const std::span<const char* const> args, const ToolStreams streams) -> int
{
  return ToolApp(info, commands).Run(args, streams);
}

ToolLogging::ToolLogging(int& argc, char** argv)
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = true;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");
}

ToolLogging::~ToolLogging()
{
  loguru::flush();
  loguru::g_global_verbosity = loguru::Verbosity_OFF;
  loguru::shutdown();
}

auto RunToolMain(const ToolInfo& info,
  const std::span<ToolCommand* const> commands, int argc, char** argv) -> int
{
  const ToolLogging logging(argc, argv);
  return RunTool(info, commands,
    std::span<const char* const>(argv, static_cast<std::size_t>(argc)),
    ToolStreams {
      .out = observer_ptr<std::ostream> { &std::cout },
      .err = observer_ptr<std::ostream> { &std::cerr },
    });
}

} // namespace oxygen::content::toolkit
