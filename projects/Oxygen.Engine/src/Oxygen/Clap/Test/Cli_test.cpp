//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>

using testing::Eq;
using testing::IsTrue;

namespace oxygen::clap {

namespace {

  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
  class BaseCli {
  public:
    [[nodiscard]] [[maybe_unused]] virtual auto CommandLine() -> Cli& = 0;

    [[nodiscard]] auto CommonOptions() const -> const std::shared_ptr<Options>&
    {
      if (!common_options_) {
        common_options_ = std::make_shared<Options>("Common options");
        common_options_->Add(Option::WithKey("help")
            .Long("help")
            .About("show this message, then exit")
            .WithValue<bool>()
            .Build());
        common_options_->Add(Option::WithKey("version")
            .About(
              fmt::format("show {} version info, then exit", ProgramName()))
            .Long("version")
            .WithValue<bool>()
            .Build());
      }
      return common_options_;
    }

  protected:
    ~BaseCli() = default;

    [[nodiscard]] virtual auto ProgramName() const -> const char* = 0;

  private:
    mutable Options::Ptr common_options_;
  };

  constexpr auto about_head
    = "Print the first 10 lines of each FILE to standard output. With more "
      "than "
      "one FILE, precede each with a header giving the file name.\n"
      "\n"
      "With no FILE, or when FILE is -, read standard input.\n"
      "\n"
      "Mandatory arguments to long options are mandatory for short options "
      "too.";

  // constexpr const auto head_command_detailed_usage =
  //     "NUM may have a multiplier suffix: b 512, kB 1000, K 1024, MB
  //     1000*1000, " "M 1024*1024, GB 1000*1000*1000, G 1024*1024*1024, and so
  //     on for T, P, E, " "Z, Y. Binary prefixes can be used, too: KiB=K,
  //     MiB=M, and so on.";

  // TODO(Abdessattar): add support for usage footer
  // constexpr const auto usage_footer =
  //     "GNU coreutils online help: <https://www.gnu.org/software/coreutils/>.
  //     " "Report head translation bugs to
  //     <https://translationproject.org/team/>. " "Full documentation at:
  //     <https://www.gnu.org/software/coreutils/head> or " "available locally
  //     via: info '(coreutils) head invocation'.";

  constexpr int default_num_lines = 10;

  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
  class HeadCli : public BaseCli {
  public:
    virtual ~HeadCli() = default;
    auto CommandLine() -> Cli& override
    {
      if (!cli_) {
        const Command::Ptr command {
          CommandBuilder(Command::DEFAULT).WithOptions(CommonOptions()).Build()
        };
        cli_ = CliBuilder()
                 .ProgramName(ProgramName())
                 .Version("1.1.0")
                 .About(about_head)
                 .WithTheme(CliTheme::Plain())
                 // TODO(Abdessattar) support usage footer
                 //.Footer(usage_footer)
                 .WithCommand(command);
      }
      return *cli_;
    }
    [[nodiscard]] auto MakeCommand(std::string name) const -> Command::Ptr
    {
      if (!command_) {
        command_
          = CommandBuilder(std::move(name))
              .About("output the first part of files")
              .WithOption(Option::WithKey("bytes")
                  .About("print the first NUM bytes of each file; with the "
                         "leading '-', print all but the last NUM bytes of "
                         "each file")
                  .Short("c")
                  .Long("bytes")
                  .WithValue<int>()
                  .Build())
              .WithOption(Option::WithKey("lines")
                  .About("print the first NUM lines instead of the "
                         "first 10; with "
                         "the leading '-', print all but the last  "
                         "NUM lines of "
                         "each file")
                  .Short("n")
                  .Long("lines")
                  .WithValue<int>()
                  .DefaultValue(default_num_lines)
                  .Build())
              .WithOption(Option::WithKey("quiet")
                  .About("never print headers giving file names")
                  .Short("q")
                  .Long("quiet")
                  // TODO(Abdessattar): support multiple name
                  // aliases .Long("silent")
                  .WithValue<bool>()
                  .Build())
              .WithOption(Option::WithKey("verbose")
                  .About("always print headers giving file names")
                  .Short("v")
                  .Long("verbose")
                  .WithValue<bool>()
                  .DefaultValue(false)
                  .Build())
              .WithOption(Option::WithKey("zero-terminated")
                  .About("line delimiter is NULL, not newline")
                  .Short("z")
                  .Long("zero-terminated")
                  .WithValue<bool>()
                  .DefaultValue(false)
                  .Build())
              .WithPositionalArguments(
                Option::Rest().WithValue<std::string>().Build())
              .Build();
      }
      return command_;
    }

  protected:
    [[nodiscard]] auto ProgramName() const -> const char* override
    {
      return "head";
    }

  private:
    mutable Command::Ptr command_;
    std::unique_ptr<Cli> cli_;
  };

  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
  class PaintCli : public BaseCli {
  public:
    enum class Color { red = 1, green = 2, blue = 3 };

    virtual ~PaintCli() = default;
    auto CommandLine() -> Cli& override
    {
      if (!cli_) {
        const std::shared_ptr<Command> command {
          CommandBuilder(Command::DEFAULT).WithOptions(CommonOptions()).Build()
        };
        cli_ = CliBuilder()
                 .ProgramName(ProgramName())
                 .Version("1.0.0")
                 .About("Paint something using a color")
                 .WithTheme(CliTheme::Plain())
                 // TODO(Abdessattar) support usage footer
                 //.Footer(usage_footer)
                 .WithCommand(command);
      }
      return *cli_;
    }
    [[nodiscard]] auto MakeCommand(std::string name) const -> Command::Ptr
    {
      if (!command_) {
        command_ = CommandBuilder(std::move(name))
                     .About("paint something")
                     .WithOption(Option::WithKey("color")
                         .About("select a color from possible "
                                "values `Red`(1), `Green`(2) "
                                "or `Blue`(3)")
                         .Short("c")
                         .Long("color")
                         .WithValue<Color>()
                         .Repeatable()
                         .Build())
                     .Build();
      }
      return command_;
    }

  protected:
    [[nodiscard]] auto ProgramName() const -> const char* override
    {
      return "paint";
    }

  private:
    mutable Command::Ptr command_;
    std::unique_ptr<Cli> cli_;
  };

  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
  class UtilsCli : public BaseCli {
  public:
    virtual ~UtilsCli() = default;
    auto CommandLine() -> Cli& override
    {
      if (!cli_) {
        const Command::Ptr default_command {
          CommandBuilder(Command::DEFAULT).WithOptions(CommonOptions()).Build()
        };
        HeadCli head;
        PaintCli paint;
        cli_ = CliBuilder()
                 .ProgramName(ProgramName())
                 .Version("1.1.0")
                 .About("GNU Core Utils - the basic file, shell and text "
                        "manipulation utilities of the GNU operating system.")
                 .WithTheme(CliTheme::Plain())
                 // TODO(Abdessattar) support usage footer
                 //.Footer(usage_footer)
                 .WithCommand(default_command)
                 .WithCommand(head.MakeCommand("head"))
                 .WithCommand(paint.MakeCommand("paint"));
      }
      return *cli_;
    }

  protected:
    [[nodiscard]] auto ProgramName() const -> const char* override
    {
      return "utils";
    }

  private:
    std::unique_ptr<Cli> cli_;
  };

  //! Scenario: Global options are parsed before the command.
  NOLINT_TEST(GlobalOptions, BeforeCommand_IsParsed)
  {
    // Arrange
    const auto global_verbose
      = Option::WithKey("verbose").Long("verbose").WithValue<bool>().Build();
    const Command::Ptr run_command = CommandBuilder("run").WithOption(
      Option::WithKey("count").Long("count").WithValue<int>().Build());
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_verbose)
                       .WithCommand(run_command)
                       .Build();

    constexpr int argc = 5;
    const char* argv[argc] = { "tool", "--verbose", "run", "--count", "3" };

    // Act
    const auto context = cli->Parse(argc, argv);

    // Assert
    const auto& ovm = context.ovm;
    EXPECT_TRUE(ovm.HasOption("verbose"));
    EXPECT_TRUE(ovm.HasOption("count"));
    EXPECT_THAT(ovm.ValuesOf("count").at(0).GetAs<int>(), Eq(3));
    EXPECT_THAT(ovm.ValuesOf("verbose").at(0).GetAs<bool>(), IsTrue());
  }

  //! Scenario: Global options after the command are rejected.
  NOLINT_TEST(GlobalOptions, AfterCommand_IsRejected)
  {
    // Arrange
    const auto global_verbose
      = Option::WithKey("verbose").Long("verbose").WithValue<bool>().Build();
    const Command::Ptr run_command = CommandBuilder("run");
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_verbose)
                       .WithCommand(run_command)
                       .Build();

    constexpr int argc = 3;
    const char* argv[argc] = { "tool", "run", "--verbose" };

    // Act & Assert
    NOLINT_EXPECT_THROW(cli->Parse(argc, argv), CmdLineArgumentsError);
  }

  //! Scenario: Command options take precedence over global options.
  NOLINT_TEST(GlobalOptions, CommandOptions_TakePrecedence)
  {
    // Arrange
    const auto global_mode
      = Option::WithKey("global_mode").Long("mode").WithValue<bool>().Build();
    const Command::Ptr run_command = CommandBuilder("run").WithOption(
      Option::WithKey("mode").Long("mode").WithValue<int>().Build());
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_mode)
                       .WithCommand(run_command)
                       .Build();

    constexpr int argc = 4;
    const char* argv[argc] = { "tool", "run", "--mode", "7" };

    // Act
    const auto context = cli->Parse(argc, argv);

    // Assert
    const auto& ovm = context.ovm;
    EXPECT_TRUE(ovm.HasOption("mode"));
    EXPECT_FALSE(ovm.HasOption("global_mode"));
    EXPECT_THAT(ovm.ValuesOf("mode").at(0).GetAs<int>(), Eq(7));
  }

  //! Scenario: Help output lists global options before command options.
  NOLINT_TEST(GlobalOptions, HelpOutput_ListsGlobalBeforeCommandOptions)
  {
    // Arrange
    const auto global_verbose
      = Option::WithKey("verbose").Long("verbose").WithValue<bool>().Build();
    const Command::Ptr run_command = CommandBuilder("run").WithOption(
      Option::WithKey("count").Long("count").WithValue<int>().Build());
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_verbose)
                       .WithCommand(run_command)
                       .WithHelpCommand()
                       .Build();

    constexpr int argc = 3;
    const char* argv[argc] = { "tool", "run", "--help" };
    testing::internal::CaptureStdout();

    // Act
    (void)cli->Parse(argc, argv);
    const auto output = testing::internal::GetCapturedStdout();

    // Assert
    const auto global_header_pos = output.find("GLOBAL OPTIONS");
    const auto command_option_pos = output.find("--count", global_header_pos);
    EXPECT_NE(global_header_pos, std::string::npos);
    EXPECT_NE(command_option_pos, std::string::npos);
    EXPECT_LT(global_header_pos, command_option_pos);
  }

  //! Scenario: Global options without a command are rejected.
  NOLINT_TEST(GlobalOptions, GlobalsOnly_NoCommand_IsRejected)
  {
    // Arrange
    const auto global_verbose
      = Option::WithKey("verbose").Long("verbose").WithValue<bool>().Build();
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_verbose)
                       .Build();

    constexpr int argc = 2;
    const char* argv[argc] = { "tool", "--verbose" };

    // Act & Assert
    NOLINT_EXPECT_THROW(cli->Parse(argc, argv), CmdLineArgumentsError);
  }

  //! Scenario: Dash-dash before any command is rejected.
  NOLINT_TEST(GlobalOptions, DashDashBeforeCommand_IsRejected)
  {
    // Arrange
    const auto global_verbose
      = Option::WithKey("verbose").Long("verbose").WithValue<bool>().Build();
    const Command::Ptr run_command = CommandBuilder("run");
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOption(global_verbose)
                       .WithCommand(run_command)
                       .Build();

    constexpr int argc = 2;
    const char* argv[argc] = { "tool", "--" };

    // Act & Assert
    NOLINT_EXPECT_THROW(cli->Parse(argc, argv), CmdLineArgumentsError);
  }

  //! Scenario: Hidden global options are not listed in help output.
  NOLINT_TEST(GlobalOptions, HiddenGlobals_AreNotListedInHelp)
  {
    // Arrange
    const auto hidden_group = std::make_shared<Options>("Hidden globals");
    hidden_group->Add(
      Option::WithKey("hidden").Long("hidden").WithValue<bool>().Build());
    const Command::Ptr run_command = CommandBuilder("run");
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .WithGlobalOptions(hidden_group, true)
                       .WithCommand(run_command)
                       .WithHelpCommand()
                       .Build();

    constexpr int argc = 3;
    const char* argv[argc] = { "tool", "run", "--help" };
    testing::internal::CaptureStdout();

    // Act
    (void)cli->Parse(argc, argv);
    const auto output = testing::internal::GetCapturedStdout();

    // Assert
    EXPECT_EQ(output.find("GLOBAL OPTIONS"), std::string::npos);
  }

  //! Scenario: Theme global option selects the output theme.
  NOLINT_TEST(GlobalOptions, ThemeSelection_UsesRequestedTheme)
  {
    // Arrange
    const Command::Ptr run_command = CommandBuilder("run");
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithThemeSelectionOption()
                       .WithCommand(run_command)
                       .WithHelpCommand()
                       .Build();

    constexpr int argc = 5;
    const char* argv[argc] = { "tool", "--theme", "plain", "run", "--help" };
    testing::internal::CaptureStdout();

    // Act
    (void)cli->Parse(argc, argv);
    const auto output = testing::internal::GetCapturedStdout();

    // Assert
    EXPECT_EQ(output.find("\x1b["), std::string::npos);
  }

  //! Scenario: Per-value notifier is called for each parsed value.
  NOLINT_TEST(Callbacks, PerValueNotifier_FiresForEachOccurrence)
  {
    // Arrange
    std::vector<int> values;
    const Command::Ptr command
      = CommandBuilder(Command::DEFAULT)
          .WithOption(Option::WithKey("tag")
              .Long("tag")
              .WithValue<int>()
              .Repeatable()
              .CallOnEachValue([&values](const int& v) { values.push_back(v); })
              .Build())
          .Build();
    const auto cli
      = CliBuilder().ProgramName("tool").WithCommand(command).Build();

    constexpr int argc = 5;
    const char* argv[argc] = { "tool", "--tag", "1", "--tag", "2" };

    // Act
    (void)cli->Parse(argc, argv);

    // Assert
    ASSERT_EQ(values.size(), 2U);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
  }

  //! Scenario: Per-value notifier is not called for defaulted values.
  NOLINT_TEST(Callbacks, PerValueNotifier_SkipsDefaults)
  {
    // Arrange
    std::vector<int> values;
    const Command::Ptr command
      = CommandBuilder(Command::DEFAULT)
          .WithOption(Option::WithKey("count")
              .Long("count")
              .WithValue<int>()
              .DefaultValue(42)
              .CallOnEachValue([&values](const int& v) { values.push_back(v); })
              .Build())
          .Build();
    const auto cli
      = CliBuilder().ProgramName("tool").WithCommand(command).Build();

    constexpr int argc = 1;
    const char* argv[argc] = { "tool" };

    // Act
    (void)cli->Parse(argc, argv);

    // Assert
    EXPECT_TRUE(values.empty());
  }

  //! Scenario: Usage footer is printed in help output.
  NOLINT_TEST(HelpOutput, Footer_IsPrinted)
  {
    // Arrange
    const auto cli = CliBuilder()
                       .ProgramName("tool")
                       .WithTheme(CliTheme::Plain())
                       .Footer("Footer text goes here.")
                       .WithHelpCommand()
                       .WithCommand(CommandBuilder(Command::DEFAULT))
                       .Build();

    constexpr int argc = 2;
    const char* argv[argc] = { "tool", "--help" };
    testing::internal::CaptureStdout();

    // Act
    (void)cli->Parse(argc, argv);
    const auto output = testing::internal::GetCapturedStdout();

    // Assert
    EXPECT_NE(output.find("FOOTER"), std::string::npos);
    EXPECT_NE(output.find("Footer text goes here."), std::string::npos);
  }

  //! Scenario: Invalid values report expected type names.
  NOLINT_TEST(ErrorReporting, InvalidValue_ReportsExpectedType)
  {
    // Arrange
    const Command::Ptr command
      = CommandBuilder(Command::DEFAULT)
          .WithOption(
            Option::WithKey("count").Long("count").WithValue<int>().Build())
          .Build();
    const auto cli
      = CliBuilder().ProgramName("tool").WithCommand(command).Build();

    constexpr int argc = 3;
    const char* argv[argc] = { "tool", "--count", "nope" };
    testing::internal::CaptureStderr();

    // Act
    NOLINT_EXPECT_THROW(cli->Parse(argc, argv), CmdLineArgumentsError);
    const auto err = testing::internal::GetCapturedStderr();

    // Assert
    EXPECT_NE(err.find("expected type 'integer'"), std::string::npos);
  }

} // namespace

} // namespace oxygen::clap
