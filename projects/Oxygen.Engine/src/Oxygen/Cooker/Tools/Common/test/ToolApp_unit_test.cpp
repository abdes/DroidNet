//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Common/ToolApp.cpp

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::observer_ptr;
using oxygen::content::toolkit::ExitCode;
using oxygen::content::toolkit::RunTool;
using oxygen::content::toolkit::ToolCommand;
using oxygen::content::toolkit::ToolContext;
using oxygen::content::toolkit::ToolError;
using oxygen::content::toolkit::ToolInfo;
using oxygen::content::toolkit::ToolLifecycle;
using oxygen::content::toolkit::ToolResult;
using oxygen::content::toolkit::ToolStreams;

constexpr int kCustomExitCode = 7;
constexpr int kParseHandlerExitCode = 9;

//! Stands in for a tool's own parse failure report.
auto ReportParseFailure(const oxygen::clap::CmdLineArgumentsError& /*error*/,
  ToolStreams streams) -> int
{
  *streams.err << "custom parse report\n";
  return kParseHandlerExitCode;
}

//! A command whose outcome the test chooses.
class ScriptedCommand final : public ToolCommand {
public:
  enum class Outcome { kSucceed, kFail, kThrow, kCustomCode };

  ScriptedCommand(std::string name, const Outcome outcome)
    : name_(std::move(name))
    , outcome_(outcome)
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override { return name_; }

  [[nodiscard]] auto BuildCommand()
    -> std::shared_ptr<oxygen::clap::Command> override
  {
    return oxygen::clap::CommandBuilder(name_).About("scripted");
  }

  [[nodiscard]] auto Run(ToolContext& context) -> ToolResult override
  {
    ++runs_;
    command_line_ = context.CommandLine();
    context.Out() << "ran " << name_ << '\n';
    switch (outcome_) {
    case Outcome::kSucceed:
      return {};
    case Outcome::kFail:
      return std::unexpected(ToolError::Usage("bad input"));
    case Outcome::kThrow:
      throw std::runtime_error("boom");
    case Outcome::kCustomCode:
      return std::unexpected(ToolError::WithCode(kCustomExitCode));
    }
    return {};
  }

  [[nodiscard]] auto Runs() const -> int { return runs_; }
  [[nodiscard]] auto CommandLine() const -> const std::string&
  {
    return command_line_;
  }

private:
  int runs_ = 0;
  std::string command_line_;
  std::string name_;
  Outcome outcome_;
};

class ToolAppTest : public testing::Test {
protected:
  static constexpr ToolInfo kInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .about = "A tool under test.",
  };

  auto Invoke(std::vector<std::string> args, ToolCommand& command,
    const ToolInfo& info = kInfo) -> int
  {
    args.insert(args.begin(), "Test.Tool");
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
      argv.push_back(arg.c_str());
    }
    std::array<ToolCommand*, 1> commands { &command };
    return RunTool(info, commands, argv,
      ToolStreams {
        .out = observer_ptr<std::ostream> { &out_ },
        .err = observer_ptr<std::ostream> { &err_ },
      });
  }

  [[nodiscard]] auto Stdout() const -> std::string { return out_.str(); }
  [[nodiscard]] auto Stderr() const -> std::string { return err_.str(); }

private:
  std::ostringstream out_;
  std::ostringstream err_;
};

//! A successful command runs once and exits 0.
NOLINT_TEST_F(ToolAppTest, SelectedCommandRunsAndSucceeds)
{
  // Arrange
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  // Act
  const auto code = Invoke({ "scan" }, command);

  // Assert
  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(command.Runs(), 1);
  EXPECT_NE(Stdout().find("ran scan"), std::string::npos);
  EXPECT_TRUE(Stderr().empty());
}

//! A command error is printed once and carries its own exit code.
NOLINT_TEST_F(ToolAppTest, CommandErrorIsReportedWithItsCode)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kFail);

  const auto code = Invoke({ "scan" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kUsage));
  EXPECT_EQ(Stderr(), "ERROR: bad input\n");
}

//! An exception escaping a command is a failure, not a crash.
NOLINT_TEST_F(ToolAppTest, EscapingExceptionIsAFailure)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kThrow);

  const auto code = Invoke({ "scan" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kFailed));
  EXPECT_EQ(Stderr(), "ERROR: boom\n");
}

//! The built-in `versions` command reports the tool identity without running
//! any registered command.
NOLINT_TEST_F(ToolAppTest, VersionsCommandPrintsToolIdentity)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  const auto code = Invoke({ "versions" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(command.Runs(), 0);
  EXPECT_NE(Stdout().find("Test.Tool"), std::string::npos);
}

//! `--json` switches `versions` to one JSON object.
NOLINT_TEST_F(ToolAppTest, VersionsCommandSupportsJson)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  const auto code = Invoke({ "versions", "--json" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(Stdout().front(), '{');
}

//! Help is a meta command: it exits 0 and runs nothing.
NOLINT_TEST_F(ToolAppTest, HelpRunsNoCommand)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  const auto code = Invoke({ "scan", "--help" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(command.Runs(), 0);
}

//! Help goes to the tool's output stream, not to the process standard output.
NOLINT_TEST_F(ToolAppTest, HelpGoesToTheToolOutput)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  testing::internal::CaptureStdout();

  const auto code = Invoke({ "--help" }, command);

  const auto process_output = testing::internal::GetCapturedStdout();
  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_THAT(Stdout(), testing::HasSubstr("scan"));
  EXPECT_TRUE(process_output.empty());
}

//! An unrecognized option is a usage error and runs nothing.
NOLINT_TEST_F(ToolAppTest, UnknownOptionIsAUsageError)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  const auto code = Invoke({ "scan", "--no-such-option" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kUsage));
  EXPECT_EQ(command.Runs(), 0);
}

//! A tool with its own exit-code contract passes its code through.
NOLINT_TEST_F(ToolAppTest, CustomExitCodeIsPassedThrough)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kCustomCode);

  const auto code = Invoke({ "scan" }, command);

  EXPECT_EQ(code, kCustomExitCode);
  EXPECT_TRUE(Stderr().empty());
}

//! The command line a command sees is the full argument list.
NOLINT_TEST_F(ToolAppTest, CommandSeesTheCommandLine)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);

  const auto code = Invoke({ "scan" }, command);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(command.CommandLine(), "Test.Tool scan");
}

//! A tool may add its own report of a command line failure, after the
//! parser's, and set its exit code.
NOLINT_TEST_F(ToolAppTest, ParseErrorHandlerFollowsTheParserReport)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .about = "A tool under test.",
    .on_parse_error = ReportParseFailure,
  };

  const auto code = Invoke({ "scan", "--no-such-option" }, command, info);

  EXPECT_EQ(code, kParseHandlerExitCode);
  EXPECT_THAT(Stderr(), testing::HasSubstr("'--no-such-option'"));
  EXPECT_THAT(Stderr(), testing::EndsWith("custom parse report\n"));
  EXPECT_TRUE(Stdout().empty());
  EXPECT_EQ(command.Runs(), 0);
}

//! A lifecycle that records how the tool brackets a command.
class RecordingLifecycle final : public ToolLifecycle {
public:
  explicit RecordingLifecycle(std::optional<int> fail_with = std::nullopt)
    : fail_with_(fail_with)
  {
  }

  [[nodiscard]] auto Begin(const oxygen::clap::CommandLineContext& /*parsed*/,
    ToolCommand& command, ToolContext& /*context*/) -> ToolResult override
  {
    ++begins_;
    began_with_ = std::string(command.Name());
    if (fail_with_.has_value()) {
      return std::unexpected(ToolError::WithCode(*fail_with_));
    }
    return {};
  }

  auto End() -> void override { ++ends_; }

  [[nodiscard]] auto Begins() const -> int { return begins_; }
  [[nodiscard]] auto Ends() const -> int { return ends_; }
  [[nodiscard]] auto BeganWith() const -> const std::string&
  {
    return began_with_;
  }

private:
  std::optional<int> fail_with_;
  int begins_ = 0;
  int ends_ = 0;
  std::string began_with_;
};

//! The lifecycle runs around the command and sees which command it is.
NOLINT_TEST_F(ToolAppTest, LifecycleBracketsTheCommand)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  RecordingLifecycle lifecycle;
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .lifecycle = observer_ptr<ToolLifecycle> { &lifecycle },
  };

  const auto code = Invoke({ "scan" }, command, info);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(lifecycle.Begins(), 1);
  EXPECT_EQ(lifecycle.BeganWith(), "scan");
  EXPECT_EQ(command.Runs(), 1);
  EXPECT_EQ(lifecycle.Ends(), 1);
}

//! The lifecycle still ends when the command fails.
NOLINT_TEST_F(ToolAppTest, LifecycleEndsAfterAFailedCommand)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kThrow);
  RecordingLifecycle lifecycle;
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .lifecycle = observer_ptr<ToolLifecycle> { &lifecycle },
  };

  const auto code = Invoke({ "scan" }, command, info);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kFailed));
  EXPECT_EQ(lifecycle.Ends(), 1);
}

//! A lifecycle that cannot begin stops the run with its code, and is not ended.
NOLINT_TEST_F(ToolAppTest, FailedLifecycleBeginSkipsTheCommand)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  RecordingLifecycle lifecycle(kCustomExitCode);
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .lifecycle = observer_ptr<ToolLifecycle> { &lifecycle },
  };

  const auto code = Invoke({ "scan" }, command, info);

  EXPECT_EQ(code, kCustomExitCode);
  EXPECT_EQ(command.Runs(), 0);
  EXPECT_EQ(lifecycle.Ends(), 0);
}

//! Help is not a run: the lifecycle is left alone.
NOLINT_TEST_F(ToolAppTest, HelpDoesNotTouchTheLifecycle)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  RecordingLifecycle lifecycle;
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .lifecycle = observer_ptr<ToolLifecycle> { &lifecycle },
  };

  const auto code = Invoke({ "scan", "--help" }, command, info);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(lifecycle.Begins(), 0);
}

//! Global option groups are accepted before the command name.
NOLINT_TEST_F(ToolAppTest, GlobalOptionsPrecedeTheCommand)
{
  ScriptedCommand command("scan", ScriptedCommand::Outcome::kSucceed);
  auto loud = false;
  auto group = std::make_shared<oxygen::clap::Options>("Global Options");
  group->Add(oxygen::clap::Option::WithKey("loud")
      .About("A global flag")
      .Long("loud")
      .WithValue<bool>()
      .DefaultValue(false, "false")
      .ImplicitValue(true, "true")
      .StoreTo(&loud)
      .Build());
  const auto groups
    = std::vector<std::shared_ptr<oxygen::clap::Options>> { group };
  const auto info = ToolInfo {
    .program = "Test.Tool",
    .version = "1.2",
    .global_options = groups,
  };

  const auto code = Invoke({ "--loud", "scan" }, command, info);

  EXPECT_EQ(code, static_cast<int>(ExitCode::kOk));
  EXPECT_EQ(command.Runs(), 1);
  EXPECT_TRUE(loud);
}

} // namespace
