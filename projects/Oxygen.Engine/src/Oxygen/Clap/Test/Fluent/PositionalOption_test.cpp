//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Fluent/PositionalOptionBuilder.h>
#include <Oxygen/Clap/Option.h>

using oxygen::clap::Option;
using oxygen::clap::PositionalOptionBuilder;
using testing::Eq;
using testing::IsFalse;
using testing::IsTrue;

namespace {

//! Scenario: Constructing a positional option with a key
NOLINT_TEST(PositionalOptionBuilder, ConstructWithKey)
{
  // Arrange & Act
  PositionalOptionBuilder builder("input");
  auto opt = builder.Build();

  // Assert
  EXPECT_THAT(opt->Key(), Eq("input"));
  EXPECT_THAT(opt->IsPositional(), IsTrue());
  EXPECT_THAT(opt->IsPositionalRest(), IsFalse());
}

//! Scenario: Setting about and user-friendly name
NOLINT_TEST(PositionalOptionBuilder, AboutAndUserFriendlyName)
{
  // Arrange
  PositionalOptionBuilder builder("file");
  std::string about = "Input file";
  std::string friendly = "File";

  // Act
  auto opt = builder.About(about).UserFriendlyName(friendly).Build();

  // Assert
  EXPECT_THAT(opt->About(), Eq(about));
  EXPECT_THAT(opt->UserFriendlyName(), Eq(friendly));
}

//! Scenario: Marking a positional option as required
NOLINT_TEST(PositionalOptionBuilder, Required)
{
  // Arrange
  PositionalOptionBuilder builder("arg");

  // Act
  auto opt = builder.Required().Build();

  // Assert
  EXPECT_THAT(opt->IsRequired(), IsTrue());
}

//! Scenario: Using WithValue to attach a value descriptor
NOLINT_TEST(PositionalOptionBuilder, WithValueAttachesDescriptor)
{
  // Arrange
  PositionalOptionBuilder builder("count");
  int store = 0;

  // Act
  auto opt = builder.WithValue<int>().StoreTo(&store).DefaultValue(42).Build();

  // Assert
  EXPECT_TRUE(opt->value_semantic() != nullptr);
}

//! Scenario: Constructing a positional rest option
NOLINT_TEST(PositionalOptionBuilder, ConstructRest)
{
  // Arrange & Act
  PositionalOptionBuilder builder(Option::key_rest_);
  auto opt = builder.Build();

  // Assert
  EXPECT_THAT(opt->Key(), Eq(Option::key_rest_));
  EXPECT_THAT(opt->IsPositional(), IsTrue());
  EXPECT_THAT(opt->IsPositionalRest(), IsTrue());
}

//! Scenario: Using builder after Build() throws
NOLINT_TEST(PositionalOptionBuilder, BuilderMethodAfterBuildThrows)
{
  // Arrange
  PositionalOptionBuilder builder("fail");
  auto opt = builder.Build();

  // Act & Assert
  EXPECT_THROW(builder.About("fail"), std::logic_error);
  EXPECT_THROW(builder.Required(), std::logic_error);
}

} // namespace
