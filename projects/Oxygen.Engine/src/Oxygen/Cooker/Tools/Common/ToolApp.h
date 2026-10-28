//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <ostream>
#include <span>
#include <vector>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::toolkit {

//! A content tool: its commands behind one command line.
/*!
 Adds the `help`, `version` and `versions` commands to those given. Everything
 the tool prints, including the parser's help and errors, goes to the streams
 given to `Run`.

 - A command line error is reported by the parser and yields `ExitCode::kUsage`,
   or what `ToolInfo::on_parse_error` returns after adding its own report.
 - A command that fails, by error or exception, is reported once on the error
   stream and yields its code.

 Does not touch process-wide state, so tests may build and run it repeatedly.
 The commands and `ToolInfo::lifecycle` must outlive the app;
 `ToolInfo::global_options` is only read while the app is built.
*/
class ToolApp {
public:
  ToolApp(const ToolInfo& info, std::span<ToolCommand* const> commands);
  ~ToolApp();
  ToolApp(const ToolApp&) = delete;
  auto operator=(const ToolApp&) -> ToolApp& = delete;
  ToolApp(ToolApp&&) = delete;
  auto operator=(ToolApp&&) -> ToolApp& = delete;

  //! The parser, for tests that check how a command line is parsed.
  [[nodiscard]] auto Cli() -> clap::Cli& { return *cli_; }

  //! Parses `args` (the whole command line, program name first), runs the
  //! selected command; returns the exit code.
  [[nodiscard]] auto Run(std::span<const char* const> args, ToolStreams streams)
    -> int;

private:
  ToolInfo info_;
  std::unique_ptr<ToolCommand> versions_;
  std::vector<ToolCommand*> commands_;
  std::unique_ptr<clap::Cli> cli_;
};

//! Runs `body` the way `ToolApp` runs a command; returns the process exit code.
/*!
 A returned error or an escaping exception is reported once on `err` as
 `ERROR: <message>`; an error without a message was already reported.
*/
[[nodiscard]] auto ExitCodeOf(
  std::ostream& err, const std::function<ToolResult()>& body) -> int;

//! Builds a `ToolApp` for `commands` and runs it once.
[[nodiscard]] auto RunTool(const ToolInfo& info,
  std::span<ToolCommand* const> commands, std::span<const char* const> args,
  ToolStreams streams) -> int;

//! Sets up and tears down the logging every content tool shares.
class ToolLogging {
public:
  //! Initializes loguru; it may consume its own flags from `argc` and `argv`.
  ToolLogging(int& argc, char** argv);
  ~ToolLogging();
  ToolLogging(const ToolLogging&) = delete;
  auto operator=(const ToolLogging&) -> ToolLogging& = delete;
  ToolLogging(ToolLogging&&) = delete;
  auto operator=(ToolLogging&&) -> ToolLogging& = delete;
};

//! `RunTool` on the standard streams, inside a `ToolLogging` scope.
[[nodiscard]] auto RunToolMain(const ToolInfo& info,
  std::span<ToolCommand* const> commands, int argc, char** argv) -> int;

} // namespace oxygen::content::toolkit
