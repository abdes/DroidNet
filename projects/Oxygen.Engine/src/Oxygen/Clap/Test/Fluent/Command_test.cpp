//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Option.h>

using testing::Eq;
using testing::IsTrue;

using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;
using oxygen::clap::Options;

namespace {

//! Scenario: Constructing a default command (empty path)
NOLINT_TEST(Command, Default)
{
  // Arrange & Act
  std::unique_ptr<Command> cmd = CommandBuilder(Command::DEFAULT);

  // Assert
  EXPECT_THAT(cmd->Path().size(), Eq(1));
  EXPECT_THAT(cmd->Path(), testing::Contains(""));
  EXPECT_THAT(cmd->IsDefault(), IsTrue());
}

//! Scenario: Constructing a command with a single path segment
NOLINT_TEST(Command, OneSegmentPath)
{
  // Arrange & Act
  std::unique_ptr<Command> cmd = CommandBuilder("path");

  // Assert
  EXPECT_THAT(cmd->Path().size(), Eq(1));
  EXPECT_THAT(cmd->Path(), testing::Contains("path"));
}

//! Scenario: Constructing a command with multiple path segments
NOLINT_TEST(Command, MultiSegmentPath)
{
  // Arrange & Act
  std::unique_ptr<Command> cmd = CommandBuilder("segment1", "segment2");

  // Assert
  EXPECT_THAT(cmd->Path().size(), Eq(2));
  EXPECT_THAT(cmd->Path(), testing::Contains("segment1"));
  EXPECT_THAT(cmd->Path(), testing::Contains("segment2"));
}

//! Scenario: Creating a command with default followed by another segment throws
NOLINT_TEST(Command, DefaultFollowedByOtherSegmentIsIllegalPath)
{
  // Act & Assert
  NOLINT_ASSERT_THROW(CommandBuilder("", "segment"), std::exception);
}

//! Scenario: Creating a command with multiple segments containing default
//! throws
NOLINT_TEST(Command, MultipleSegmentsContainingDefaultIsIllegalPath)
{
  // Act & Assert
  NOLINT_ASSERT_THROW(
    CommandBuilder("sgement1", "", "segment2"), std::exception);
}

//! Scenario: Setting the about string using CommandBuilder::About()
NOLINT_TEST(Command, AboutSetsAboutString)
{
  // Arrange
  std::string about_text = "This is a test command";

  // Act
  std::unique_ptr<Command> cmd = CommandBuilder("test").About(about_text);

  // Assert
  EXPECT_THAT(cmd->About(), Eq(about_text));
}

//! Scenario: Attaching options using CommandBuilder::WithOptions()
NOLINT_TEST(Command, WithOptionsAttachesOptions)
{
  // Arrange
  auto opt1 = Option::WithKey("--foo").Build();
  auto opt2 = Option::WithKey("--bar").Build();
  auto options = std::make_shared<Options>("group");
  options->Add(opt1);
  options->Add(opt2);

  // Act
  std::unique_ptr<Command> cmd
    = CommandBuilder("test").WithOptions(options, true);

  // Assert
  const auto& opts = cmd->CommandOptions();
  EXPECT_THAT(opts, testing::Contains(opt1));
  EXPECT_THAT(opts, testing::Contains(opt2));
  EXPECT_THAT(opts.size(), Eq(2));
}

//! Scenario: Attaching a single option using CommandBuilder::WithOption()
NOLINT_TEST(Command, WithOptionAttachesOption)
{
  // Arrange
  auto option = Option::WithKey("--flag").Build();

  // Act
  std::unique_ptr<Command> cmd = CommandBuilder("test").WithOption(option);

  // Assert
  const auto& opts = cmd->CommandOptions();
  EXPECT_THAT(opts, testing::Contains(option));
  EXPECT_THAT(opts.size(), Eq(1));
}

//! Scenario: Attaching positional arguments using
//! CommandBuilder::WithPositionalArguments()
NOLINT_TEST(Command, WithPositionalArgumentsAttachesArguments)
{
  // Arrange
  auto option1 = Option::Positional("arg1").Build();
  auto option2 = Option::Positional("arg2").Build();

  // Act
  std::unique_ptr<Command> cmd
    = CommandBuilder("test").WithPositionalArguments(option1, option2);

  // Assert
  const auto& pos_args = cmd->PositionalArguments();
  EXPECT_THAT(pos_args, testing::Contains(option1));
  EXPECT_THAT(pos_args, testing::Contains(option2));
  EXPECT_THAT(pos_args.size(), Eq(2));
}

//! Scenario: Using CommandBuilder after Build() throws
NOLINT_TEST(Command, BuilderMethodAfterBuildThrows)
{
  // Arrange
  CommandBuilder builder("test");
  std::unique_ptr<Command> cmd = builder.Build();

  // Act & Assert
  EXPECT_THROW(builder.About("fail"), std::logic_error);
  EXPECT_THROW(
    builder.WithOption(Option::WithKey("--fail").Build()), std::logic_error);
}

//! Scenario: WithOption(nullptr) throws std::invalid_argument
NOLINT_TEST(Command, WithOptionNullptrThrows)
{
  // Arrange
  CommandBuilder builder("test");
  std::shared_ptr<Option> null_option;

  // Act & Assert
  EXPECT_THROW(builder.WithOption(null_option), std::invalid_argument);
}

//! Scenario: WithOptions(nullptr) throws std::invalid_argument
NOLINT_TEST(Command, WithOptionsNullptrThrows)
{
  // Arrange
  CommandBuilder builder("test");
  std::shared_ptr<Options> null_options;

  // Act & Assert
  EXPECT_THROW(builder.WithOptions(null_options), std::invalid_argument);
}

//! Scenario: Implicit conversion to std::unique_ptr<Command> and
//! std::shared_ptr<Command>
NOLINT_TEST(Command, ImplicitConversionToUniqueAndSharedPtr)
{
  // Arrange & Act
  std::unique_ptr<Command> unique_cmd = CommandBuilder("test");
  std::shared_ptr<Command> shared_cmd = CommandBuilder("test");

  // Assert
  EXPECT_TRUE(unique_cmd != nullptr);
  EXPECT_TRUE(shared_cmd != nullptr);
}

//! Scenario: Facet/protected constructor coverage: derived builder shares
//! encapsulated command
class CustomFacetBuilder : public CommandBuilder {
public:
  explicit CustomFacetBuilder(std::unique_ptr<Command> cmd)
    : CommandBuilder(std::move(cmd))
  {
  }
  auto CustomFacetMethod() -> CustomFacetBuilder& { return *this; }
};

//! Scenario: Facet/protected constructor coverage: derived builder shares
//! encapsulated command
NOLINT_TEST(Command, FacetBuilderSharesCommand)
{
  // Arrange
  CommandBuilder base_builder("facet");
  std::unique_ptr<Command> cmd = base_builder.Build();
  CustomFacetBuilder facet_builder(std::move(cmd));

  // Act
  facet_builder.CustomFacetMethod();

  // Assert
  // (No crash, command is shared)
  EXPECT_TRUE(true);
}

} // namespace
