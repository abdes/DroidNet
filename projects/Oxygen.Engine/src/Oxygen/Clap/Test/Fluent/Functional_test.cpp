//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;
using oxygen::clap::OptionBuilder;
using oxygen::clap::Options;
using oxygen::clap::PositionalOptionBuilder;

namespace {

// Scenario: Simulate the CLI for `grep [OPTIONS] PATTERN [FILE...]`
// This is a classic Unix tool: grep -i -v -e PATTERN file1 file2
// We want: grep -i -v -e "pattern" file1 file2
NOLINT_TEST(Functional, GrepStyleCommand)
{
  // clang-format off
  // Arrange & Act
  auto cmd = CommandBuilder("grep")
    .About("Search for PATTERN in each FILE.")
    .WithOption(
      OptionBuilder("-i")
        .About("Ignore case distinctions")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("-v")
        .About("Invert match")
        .WithValue<bool>()
        .ImplicitValue(false)
        .Build())
    .WithOption(
      OptionBuilder("-e")
        .About("Pattern to match")
        .WithValue<std::string>()
        .UserFriendlyName("PATTERN")
        .DefaultValue(std::string(""), "")
        .Required()
        .Build())
    .WithPositionalArguments(
      PositionalOptionBuilder("pattern").About("Search pattern").Build(),
      PositionalOptionBuilder("file").About("Input file(s)").Build())
    .Build();
  // clang-format on

  // Assert
  EXPECT_EQ(cmd->Path().size(), 1);
  EXPECT_EQ(cmd->Path().front(), "grep");
  EXPECT_EQ(cmd->About(), "Search for PATTERN in each FILE.");
  EXPECT_EQ(cmd->CommandOptions().size(), 3);
  EXPECT_EQ(cmd->PositionalArguments().size(), 2);
}

// Scenario: Simulate the CLI for `robocopy [source] [dest] [file [file] ...]
// [options]` This is a Windows tool: robocopy src dst file1 file2 /S /E
NOLINT_TEST(Functional, RobocopyStyleCommand)
{
  // clang-format off
  // Arrange & Act
  auto cmd = CommandBuilder("robocopy")
    .About("Robust file copy for Windows")
    .WithPositionalArguments(
      PositionalOptionBuilder("source").About("Source directory").Build(),
      PositionalOptionBuilder("dest").About("Destination directory").Build(),
      PositionalOptionBuilder("file").About("File(s) to copy").Build())
    .WithOption(
      OptionBuilder("/S")
        .About("Copy subdirectories")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("/E")
        .About("Copy subdirectories, including empty ones")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .Build();
  // clang-format on

  // Assert
  EXPECT_EQ(cmd->Path().size(), 1);
  EXPECT_EQ(cmd->Path().front(), "robocopy");
  EXPECT_EQ(cmd->About(), "Robust file copy for Windows");
  EXPECT_EQ(cmd->CommandOptions().size(), 2);
  EXPECT_EQ(cmd->PositionalArguments().size(), 3);
}

// Scenario: Simulate a partial CLI for `git` with commit, pull, and push
// subcommands using CliBuilder Each subcommand has its own options, e.g.:
//   git commit -m "msg" --amend
//   git pull --rebase
//   git push --force
NOLINT_TEST(Functional, GitStyleCommandLine)
{
  // clang-format off
  // Arrange & Act
  auto cli = oxygen::clap::CliBuilder()
    .ProgramName("git")
    .About("Distributed version control system")
    .WithCommand(CommandBuilder("commit")
      .About("Record changes to the repository")
      .WithOption(
        OptionBuilder("-m")
          .About("Commit message")
          .WithValue<std::string>()
          .UserFriendlyName("MESSAGE")
          .Required()
          .Build())
      .WithOption(
        OptionBuilder("--amend")
          .About("Amend previous commit")
          .WithValue<bool>()
          .ImplicitValue(true)
          .Build())
      .Build())
    .WithCommand(CommandBuilder("pull")
      .About("Fetch from and integrate with another repository or a local branch")
      .WithOption(
        OptionBuilder("--rebase")
          .About("Rebase after fetching")
          .WithValue<bool>()
          .ImplicitValue(true)
          .Build())
      .Build())
    .WithCommand(CommandBuilder("push")
      .About("Update remote refs along with associated objects")
      .WithOption(
        OptionBuilder("--force")
          .About("Force push")
          .WithValue<bool>()
          .ImplicitValue(true)
          .Build())
      .Build())
    .Build();
  // clang-format on

  // Assert
  EXPECT_EQ(cli->ProgramName(), "git");
  EXPECT_EQ(cli->About(), "Distributed version control system");
  // We cannot directly check subcommands without a public API, but we can check
  // the CLI is valid
  EXPECT_TRUE(cli != nullptr);
}

// Scenario: Simulate the CLI for `cp SOURCE DEST`
// This is a classic Unix tool: cp file1.txt file2.txt
NOLINT_TEST(Functional, CpStyleCommand)
{
  // clang-format off
  // Arrange & Act
  auto cmd = CommandBuilder("cp")
    .About("Copy SOURCE to DEST")
    .WithPositionalArguments(
      PositionalOptionBuilder("SOURCE")
        .About("Source file")
        .UserFriendlyName("SRC")
        .Required()
        .Build(),
      PositionalOptionBuilder("DEST")
        .About("Destination file")
        .UserFriendlyName("DST")
        .Required()
        .Build())
    .Build();
  // clang-format on

  // Assert
  EXPECT_EQ(cmd->Path().front(), "cp");
  EXPECT_EQ(cmd->About(), "Copy SOURCE to DEST");
  EXPECT_EQ(cmd->PositionalArguments().size(), 2);
}

// Scenario: Simulate the CLI for `mkdir [OPTION]... DIRECTORY...`
// This models the Unix mkdir command with all major options and value
// semantics.
NOLINT_TEST(Functional, MkdirStyleCommand)
{
  // clang-format off
  // Arrange & Act
  auto cmd = CommandBuilder("mkdir")
    .About("Create the DIRECTORY(ies), if they do not already exist.")
    .WithOption(
      OptionBuilder("-m")
        .About("set file mode (as in chmod), not a=rwx - umask")
        .WithValue<std::string>()
        .UserFriendlyName("MODE")
        .Build())
    .WithOption(
      OptionBuilder("--mode")
        .About("set file mode (as in chmod), not a=rwx - umask")
        .WithValue<std::string>()
        .UserFriendlyName("MODE")
        .Build())
    .WithOption(
      OptionBuilder("-p")
        .About("no error if existing, make parent directories as needed, with their file modes unaffected by any -m option")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("--parents")
        .About("no error if existing, make parent directories as needed, with their file modes unaffected by any -m option")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("-v")
        .About("print a message for each created directory")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("--verbose")
        .About("print a message for each created directory")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("-Z")
        .About("set SELinux security context of each created directory to the default type")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("--context")
        .About("like -Z, or if CTX is specified then set the SELinux or SMACK security context to CTX")
        .WithValue<std::string>()
        .UserFriendlyName("CTX")
        .DefaultValue(std::string(""), "")
        .Build())
    .WithOption(
      OptionBuilder("--help")
        .About("display this help and exit")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithOption(
      OptionBuilder("--version")
        .About("output version information and exit")
        .WithValue<bool>()
        .ImplicitValue(true)
        .Build())
    .WithPositionalArguments(
      PositionalOptionBuilder("DIRECTORY")
        .About("Directory to create")
        .UserFriendlyName("DIRECTORY")
        .Required()
        .Build())
    .Build();
  // clang-format on

  // Assert
  EXPECT_EQ(cmd->Path().size(), 1);
  EXPECT_EQ(cmd->Path().front(), "mkdir");
  EXPECT_EQ(
    cmd->About(), "Create the DIRECTORY(ies), if they do not already exist.");
  EXPECT_EQ(cmd->CommandOptions().size(), 10);
  EXPECT_EQ(cmd->PositionalArguments().size(), 1);
}

} // namespace
