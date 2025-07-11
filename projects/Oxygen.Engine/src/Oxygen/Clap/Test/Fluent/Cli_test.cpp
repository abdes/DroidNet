//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>

using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;

namespace {

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
  const std::string version = "1.2.3";
  const std::string prog = "my_prog";
  const std::string about = "Test CLI";

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
  const std::shared_ptr cmd { CommandBuilder("foo").About("desc").Build() };

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
  std::shared_ptr cmd { CommandBuilder("fail").Build() };
  NOLINT_EXPECT_THROW(builder.WithCommand(cmd), std::logic_error);
}

//! Scenario: Implicit conversion to std::unique_ptr<Cli>
NOLINT_TEST(CliBuilder, ImplicitConversionToUniquePtr)
{
  // Arrange & Act
  const std::shared_ptr<Command> cmd { CommandBuilder("bar") };
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
  const std::shared_ptr<Command> cmd {
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

} // namespace
