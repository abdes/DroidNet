//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <fmt/format.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Cli.h>
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

  // NOLINTNEXTLINE
  TEST(CommandLineTest, Test)
  {
    {
      constexpr size_t argc = 6;
      std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
        "head", "-n", "+20", "-q", "file.txt" } };

      UtilsCli cli;
      const auto& matches = cli.CommandLine().Parse(argc, argv.data()).ovm;

      const auto& v_lines = matches.ValuesOf(("lines"));
      EXPECT_THAT(v_lines.size(), Eq(1));
      EXPECT_THAT(v_lines.at(0).GetAs<int>(), Eq(20));

      const auto& v_quiet = matches.ValuesOf(("quiet"));
      EXPECT_THAT(v_quiet.size(), Eq(1));
      EXPECT_THAT(v_quiet.at(0).GetAs<bool>(), Eq(true));

      const auto& v_verbose = matches.ValuesOf(("verbose"));
      EXPECT_THAT(v_verbose.size(), Eq(1));
      EXPECT_THAT(v_verbose.at(0).GetAs<bool>(), Eq(false));

      const auto& v_zero_terminated = matches.ValuesOf(("zero-terminated"));
      EXPECT_THAT(v_zero_terminated.size(), Eq(1));
      EXPECT_THAT(v_zero_terminated.at(0).GetAs<bool>(), Eq(false));

      const auto& v_rest = matches.ValuesOf(Option::key_rest_);
      EXPECT_THAT(v_rest.size(), Eq(1));
      EXPECT_THAT(v_rest.at(0).GetAs<std::string>(), Eq("file.txt"));
    }
    {
      constexpr size_t argc = 5;
      std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
        "head", "--lines=+20", "--quiet", "file.txt" } };

      UtilsCli cli;
      const auto& matches = cli.CommandLine().Parse(argc, argv.data()).ovm;

      const auto& v_lines = matches.ValuesOf(("lines"));
      EXPECT_THAT(v_lines.size(), Eq(1));
      EXPECT_THAT(v_lines.at(0).GetAs<int>(), Eq(20));

      const auto& v_quiet = matches.ValuesOf(("quiet"));
      EXPECT_THAT(v_quiet.size(), Eq(1));
      EXPECT_THAT(v_quiet.at(0).GetAs<bool>(), Eq(true));

      const auto& v_rest = matches.ValuesOf(Option::key_rest_);
      EXPECT_THAT(v_rest.size(), Eq(1));
      EXPECT_THAT(v_rest.at(0).GetAs<std::string>(), Eq("file.txt"));
    }
    {
      constexpr size_t argc = 2;
      std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
        "--version" } };

      HeadCli cli;
      const auto& matches = cli.CommandLine().Parse(argc, argv.data()).ovm;
      const auto& values = matches.ValuesOf(("version"));
      EXPECT_THAT(values.size(), Eq(1));
      EXPECT_THAT(values.at(0).GetAs<bool>(), IsTrue());
    }
    {
      constexpr size_t argc = 2;
      std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
        "--version" } };

      UtilsCli cli;
      const auto& matches = cli.CommandLine().Parse(argc, argv.data()).ovm;
      const auto& values = matches.ValuesOf(("version"));
      EXPECT_THAT(values.size(), Eq(1));
      EXPECT_THAT(values.at(0).GetAs<bool>(), IsTrue());
    }
    {
      constexpr size_t argc = 8;
      std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
        "paint", "-c", "red", "--color=GREEN", "--color=bLue", "--color=1",
        "--color=3" } };

      UtilsCli cli;
      const auto& matches = cli.CommandLine().Parse(argc, argv.data()).ovm;
      const auto& values = matches.ValuesOf(("color"));
      EXPECT_THAT(values.size(), Eq(5));
      EXPECT_THAT(
        values.at(0).GetAs<PaintCli::Color>(), Eq(PaintCli::Color::red));
      EXPECT_THAT(
        values.at(1).GetAs<PaintCli::Color>(), Eq(PaintCli::Color::green));
      EXPECT_THAT(
        values.at(2).GetAs<PaintCli::Color>(), Eq(PaintCli::Color::blue));
    }
  }

} // namespace

} // namespace oxygen::clap
