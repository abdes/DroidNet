//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Option.h>

using testing::Eq;
using testing::IsTrue;
using testing::StrEq;

using oxygen::clap::Option;
using oxygen::clap::OptionBuilder;

namespace {

//! Scenario: Constructing an option with a key
NOLINT_TEST(OptionBuilder, WithKeySetsKey)
{
  // Arrange & Act
  auto opt = Option::WithKey("--foo").Build();

  // Assert
  EXPECT_THAT(opt->Key(), StrEq("--foo"));
}

//! Scenario: Setting short and long names via OptionBuilder
NOLINT_TEST(OptionBuilder, SetsShortAndLongNames)
{
  // Arrange & Act
  auto opt = OptionBuilder("foo").Short("-f").Long("--foo").Build();

  // Assert
  EXPECT_THAT(opt->Short(), StrEq("-f"));
  EXPECT_THAT(opt->Long(), StrEq("--foo"));
}

//! Scenario: Setting about string via OptionBuilder
NOLINT_TEST(OptionBuilder, SetsAboutString)
{
  // Arrange & Act
  auto opt = OptionBuilder("foo").About("desc").Build();

  // Assert
  EXPECT_THAT(opt->About(), StrEq("desc"));
}

//! Scenario: Setting required flag via OptionBuilder
NOLINT_TEST(OptionBuilder, SetsRequiredFlag)
{
  // Arrange & Act
  auto opt = OptionBuilder("foo").Required().Build();

  // Assert
  EXPECT_THAT(opt->IsRequired(), IsTrue());
}

//! Scenario: Setting user friendly name via OptionBuilder
NOLINT_TEST(OptionBuilder, SetsUserFriendlyName)
{
  // Arrange & Act
  auto opt = OptionBuilder("foo").UserFriendlyName("FOO").Build();

  // Assert
  EXPECT_THAT(opt->UserFriendlyName(), StrEq("FOO"));
}

//! Scenario: Using OptionBuilder after Build() throws
NOLINT_TEST(OptionBuilder, MethodAfterBuildThrows)
{
  // Arrange
  OptionBuilder builder("foo");
  auto opt = builder.Build();

  // Act & Assert
  EXPECT_THROW(builder.Short("-f"), std::logic_error);
  EXPECT_THROW(builder.Long("--foo"), std::logic_error);
  EXPECT_THROW(builder.About("desc"), std::logic_error);
  EXPECT_THROW(builder.Required(), std::logic_error);
  EXPECT_THROW(builder.UserFriendlyName("FOO"), std::logic_error);
}

} // namespace
