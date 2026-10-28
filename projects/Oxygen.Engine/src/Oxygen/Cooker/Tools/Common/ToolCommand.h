//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Command.h>

namespace oxygen::clap {
class CmdLineArgumentsError;
class OptionValuesMap;
struct CommandLineContext;
struct Options;
} // namespace oxygen::clap

namespace oxygen::content::toolkit {

//! The process exit codes shared by the content tools.
/*!
 A tool with a richer contract, such as PakTool, reports its own codes through
 `ToolError::WithCode`; these remain the common 0, 1 and 2.
*/
enum class ExitCode : int {
  kOk = 0,
  //! The command line or an input it names is invalid.
  kUsage = 1,
  //! The command ran and failed.
  kFailed = 2,
};

//! Why a command failed, and the process exit code to report for it.
struct ToolError {
  int code = static_cast<int>(ExitCode::kFailed);
  //! Printed once after `ERROR: `; leave empty if the command already reported.
  std::string message;

  [[nodiscard]] static auto Usage(std::string message = {}) -> ToolError
  {
    return {
      .code = static_cast<int>(ExitCode::kUsage),
      .message = std::move(message),
    };
  }

  [[nodiscard]] static auto Failed(std::string message = {}) -> ToolError
  {
    return {
      .code = static_cast<int>(ExitCode::kFailed),
      .message = std::move(message),
    };
  }

  [[nodiscard]] static auto WithCode(int code, std::string message = {})
    -> ToolError
  {
    return { .code = code, .message = std::move(message) };
  }
};

using ToolResult = std::expected<void, ToolError>;

//! The output streams of a tool run, injected so tests can capture them.
/*!
 A struct of named fields rather than two stream parameters, so a caller cannot
 swap them.
*/
struct ToolStreams {
  observer_ptr<std::ostream> out;
  observer_ptr<std::ostream> err;
};

//! Adds a tool's own report of a command line that failed to parse, after
//! the parser's; returns the exit code.
using ParseErrorHandler
  = auto (*)(const clap::CmdLineArgumentsError&, ToolStreams) -> int;

class ToolCommand;
class ToolContext;

//! Brackets the selected command's run, for setup and teardown the whole tool
//! shares, such as the services its commands run on.
/*!
 It runs for the tool's own commands, not for `help` and `version`.
*/
class ToolLifecycle {
public:
  ToolLifecycle() = default;
  virtual ~ToolLifecycle() = default;
  ToolLifecycle(const ToolLifecycle&) = delete;
  auto operator=(const ToolLifecycle&) -> ToolLifecycle& = delete;
  ToolLifecycle(ToolLifecycle&&) = delete;
  auto operator=(ToolLifecycle&&) -> ToolLifecycle& = delete;

  //! Runs after parsing and before the command. A returned error ends the run
  //! with its code, and `End` is not called.
  [[nodiscard]] virtual auto Begin(const clap::CommandLineContext& /*parsed*/,
    ToolCommand& /*command*/, ToolContext& /*context*/) -> ToolResult
  {
    return {};
  }

  //! Runs after the command, whatever its outcome, once `Begin` succeeded.
  virtual auto End() -> void { }
};

//! Identity of a tool, as shown by `--help`, `--version` and `versions`.
struct ToolInfo {
  std::string_view program;
  std::string_view version;
  std::string_view about;
  //! Whether the tool cooks content, so `versions` reports importer revisions.
  bool cooks_content = false;
  //! Reports a command line failure after the parser does, and picks its exit
  //! code; without it, the code is `ExitCode::kUsage`.
  ParseErrorHandler on_parse_error = nullptr;
  //! Option groups every command accepts, before the command name. Read only
  //! while the app is built.
  std::span<const std::shared_ptr<clap::Options>> global_options;
  //! Set up before and torn down after the selected command, if any. Must
  //! outlive the app.
  observer_ptr<ToolLifecycle> lifecycle;
};

//! What a command may use while it runs.
class ToolContext {
public:
  ToolContext(const ToolStreams streams, const clap::OptionValuesMap& options,
    std::string command_line)
    : streams_(streams)
    , options_(&options)
    , command_line_(std::move(command_line))
  {
  }

  [[nodiscard]] auto Streams() const -> ToolStreams { return streams_; }
  [[nodiscard]] auto Out() const -> std::ostream& { return *streams_.out; }
  [[nodiscard]] auto Err() const -> std::ostream& { return *streams_.err; }
  [[nodiscard]] auto Options() const -> const clap::OptionValuesMap&
  {
    return *options_;
  }
  //! The arguments the tool was started with, space separated.
  [[nodiscard]] auto CommandLine() const -> const std::string&
  {
    return command_line_;
  }

private:
  ToolStreams streams_;
  observer_ptr<const clap::OptionValuesMap> options_;
  std::string command_line_;
};

//! One sub-command of a content tool.
/*!
 A command owns its parsed option values as members and registers them in
 `BuildCommand`. `Run` reports failure by returning a `ToolError`; an escaping
 exception is reported as `ExitCode::kFailed`.
*/
class ToolCommand {
public:
  ToolCommand() = default;
  virtual ~ToolCommand() = default;
  ToolCommand(const ToolCommand&) = delete;
  auto operator=(const ToolCommand&) -> ToolCommand& = delete;
  ToolCommand(ToolCommand&&) = delete;
  auto operator=(ToolCommand&&) -> ToolCommand& = delete;

  //! The command path, or `clap::Command::DEFAULT` for the default command.
  [[nodiscard]] virtual auto Name() const -> std::string_view = 0;
  [[nodiscard]] virtual auto BuildCommand()
    -> std::shared_ptr<clap::Command> = 0;
  [[nodiscard]] virtual auto Run(ToolContext& context) -> ToolResult = 0;
};

} // namespace oxygen::content::toolkit
