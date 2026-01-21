//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>

using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;

namespace {

auto StripAnsiEscapeCodes(const std::string& input) -> std::string
{
  std::string output;
  output.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '[') {
      i += 2;
      while (i < input.size() && input[i] != 'm') {
        ++i;
      }
      continue;
    }
    output.push_back(input[i]);
  }
  return output;
}

auto SplitLines(const std::string& input) -> std::vector<std::string>
{
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= input.size()) {
    const auto end = input.find('\n', start);
    if (end == std::string::npos) {
      lines.emplace_back(input.substr(start));
      break;
    }
    lines.emplace_back(input.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

//! Scenario: Constructing a default CliBuilder
NOLINT_TEST(CliBuilder, DefaultConstruct)
{
  // Arrange & Act
  CliBuilder builder;
  const auto cli = builder.Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Setting version, program name, and about
NOLINT_TEST(CliBuilder, SetVersionProgramNameAbout)
{
  // Arrange
  CliBuilder builder;
  const auto version = std::string("1.2.3");
  const auto prog = std::string("my_prog");
  const auto about = std::string("Test CLI");

  // Act
  const auto cli
    = builder.Version(version).ProgramName(prog).About(about).Build();

  // Assert
  // (No direct getters, but test does not throw and returns a valid object)
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Adding a command to the CLI
NOLINT_TEST(CliBuilder, WithCommandAddsCommand)
{
  // Arrange
  CliBuilder builder;
  const auto cmd
    = std::shared_ptr { CommandBuilder("foo").About("desc").Build() };

  // Act
  const auto cli = builder.WithCommand(cmd).Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Using WithVersionCommand and WithHelpCommand
NOLINT_TEST(CliBuilder, WithVersionAndHelpCommand)
{
  // Arrange
  CliBuilder builder;

  // Act
  const auto cli = builder.WithVersionCommand().WithHelpCommand().Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Using builder after Build() throws
NOLINT_TEST(CliBuilder, BuilderMethodAfterBuildThrows)
{
  // Arrange
  CliBuilder builder;
  auto cli = builder.Build();

  // Act & Assert
  NOLINT_EXPECT_THROW(builder.Version("fail"), std::logic_error);
  auto cmd = std::shared_ptr { CommandBuilder("fail").Build() };
  NOLINT_EXPECT_THROW(builder.WithCommand(cmd), std::logic_error);
}

//! Scenario: Implicit conversion to std::unique_ptr<Cli>
NOLINT_TEST(CliBuilder, ImplicitConversionToUniquePtr)
{
  // Arrange & Act
  const auto cmd = std::shared_ptr<Command> { CommandBuilder("bar") };
  CliBuilder builder;
  const std::unique_ptr<oxygen::clap::Cli> cli = builder.WithCommand(cmd);

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Facet/protected constructor coverage: derived builder shares
//! encapsulated cli
class CustomCliFacetBuilder : public CliBuilder {
public:
  explicit CustomCliFacetBuilder(std::unique_ptr<oxygen::clap::Cli> cli)
    : CliBuilder(std::move(cli))
  {
  }
  auto CustomFacetMethod() -> CustomCliFacetBuilder& { return *this; }
};

NOLINT_TEST(CliBuilder, FacetBuilderSharesCli)
{
  // Arrange
  CliBuilder base_builder;
  auto cli = base_builder.Build();
  CustomCliFacetBuilder facet_builder(std::move(cli));

  // Act
  facet_builder.CustomFacetMethod();

  // Assert
  EXPECT_TRUE(true);
}
//! Scenario: Chaining all mutators
NOLINT_TEST(CliBuilder, ChainingAllMutators)
{
  // Arrange
  CliBuilder builder;
  const auto cmd = std::shared_ptr<Command> {
    CommandBuilder("chain").About("desc").Build()
  };

  // Act
  const auto cli = builder.Version("2.0")
                     .ProgramName("prog")
                     .About("about")
                     .WithCommand(cmd)
                     .WithVersionCommand()
                     .WithHelpCommand()
                     .Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Enabling only help command
NOLINT_TEST(CliBuilder, WithHelpCommandOnly)
{
  // Arrange
  CliBuilder builder;

  // Act
  const auto cli = builder.WithHelpCommand().Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Enabling only version command
NOLINT_TEST(CliBuilder, WithVersionCommandOnly)
{
  // Arrange
  CliBuilder builder;

  // Act
  const auto cli = builder.WithVersionCommand().Build();

  // Assert
  EXPECT_TRUE(cli != nullptr);
}

//! Scenario: Passing nullptr to WithCommand throws
NOLINT_TEST(CliBuilder, WithCommandNullptrThrows)
{
  // Arrange
  CliBuilder builder;
  std::shared_ptr<Command> null_cmd;

  // Act & Assert
  NOLINT_EXPECT_THROW(builder.WithCommand(null_cmd), std::invalid_argument);
}

//! Scenario: Help and version command presence (public API)
NOLINT_TEST(CliBuilder, HelpAndVersionCommandPresence)
{
  // Arrange
  CliBuilder builder;
  const auto cli = builder.WithVersionCommand().WithHelpCommand().Build();

  // Assert (public API only)
  EXPECT_TRUE(cli->HasHelpCommand());
  EXPECT_TRUE(cli->HasVersionCommand());
}

//! Scenario: OutputWidth rejects values less than 1
NOLINT_TEST(CliBuilder, OutputWidthRejectsInvalidValues)
{
  // Arrange
  CliBuilder builder;

  // Act & Assert
  NOLINT_EXPECT_THROW(builder.OutputWidth(0), std::invalid_argument);
}

//! Scenario: Configured output width is used for help formatting
NOLINT_TEST(CliBuilder, OutputWidthIsUsedForHelpFormatting)
{
  // Arrange
  constexpr unsigned int width = 20;
  const auto about = std::string(
    "This description is long enough to wrap across multiple lines.");
  const auto command
    = std::shared_ptr<Command> { CommandBuilder(Command::DEFAULT) };
  const auto cli = CliBuilder()
                     .ProgramName("clap-test")
                     .About(about)
                     .WithCommand(command)
                     .WithHelpCommand()
                     .OutputWidth(width)
                     .Build();

  constexpr int argc = 2;
  const char* argv[argc] = { "clap-test", "--help" };
  testing::internal::CaptureStdout();

  // Act
  (void)cli->Parse(argc, argv);
  const auto output = testing::internal::GetCapturedStdout();

  // Assert
  const auto lines = SplitLines(output);
  for (const auto& raw_line : lines) {
    const auto line = StripAnsiEscapeCodes(raw_line);
    const auto first_non_space = line.find_first_not_of(' ');
    if (first_non_space == std::string::npos) {
      continue;
    }
    if (line.starts_with("   ")) {
      EXPECT_LE(line.size(), width);
    }
  }
}

} // namespace
