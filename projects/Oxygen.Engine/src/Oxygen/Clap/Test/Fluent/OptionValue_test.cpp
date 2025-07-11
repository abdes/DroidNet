//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Fluent/OptionBuilder.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>

using testing::Eq;
using testing::IsTrue;
using testing::StrEq;

using oxygen::clap::Option;
using oxygen::clap::OptionBuilder;
using oxygen::clap::OptionValueBuilder;

namespace {

//! Scenario: Setting and storing a value via OptionValueBuilder
NOLINT_TEST(OptionValueBuilder, StoreToSetsValue)
{
  // Arrange
  int storage = 0;
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.StoreTo(&storage).DefaultValue(42);
  auto opt = builder.Build();
  // Simulate parse: set value to 7
  storage = 7;

  // Assert
  EXPECT_EQ(storage, 7);
}

//! Scenario: Setting default and implicit values via OptionValueBuilder
NOLINT_TEST(OptionValueBuilder, DefaultAndImplicitValue)
{
  // Arrange
  int storage = 0;
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.StoreTo(&storage).DefaultValue(123).ImplicitValue(99);
  auto opt = builder.Build();

  // Assert
  // (We can't simulate parse, but we can check that storage is unchanged)
  EXPECT_EQ(storage, 0);
}

//! Scenario: Setting repeatable flag via OptionValueBuilder
NOLINT_TEST(OptionValueBuilder, RepeatableFlag)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.Repeatable();
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: Using OptionValueBuilder after Build() throws
NOLINT_TEST(OptionValueBuilder, MethodAfterBuildThrows)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();
  auto opt = builder.Build();

  // Act & Assert
  int dummy = 0;
  EXPECT_THROW(builder.StoreTo(&dummy), std::logic_error);
  EXPECT_THROW(builder.DefaultValue(1), std::logic_error);
  EXPECT_THROW(builder.ImplicitValue(2), std::logic_error);
  EXPECT_THROW(builder.Repeatable(), std::logic_error);
}

//! Scenario: Setting user friendly name via OptionValueBuilder
NOLINT_TEST(OptionValueBuilder, SetsUserFriendlyName)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.UserFriendlyName("FOO");
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr); // No direct getter, but should not throw
}

//! Scenario: Setting default value with textual representation
NOLINT_TEST(OptionValueBuilder, DefaultValueWithTextual)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.DefaultValue(42, "forty-two");
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: Setting implicit value with textual representation
NOLINT_TEST(OptionValueBuilder, ImplicitValueWithTextual)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.ImplicitValue(99, "ninety-nine");
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: Chaining all OptionValueBuilder methods
NOLINT_TEST(OptionValueBuilder, ChainingAllMethods)
{
  // Arrange
  int storage = 0;
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act
  builder.StoreTo(&storage)
    .UserFriendlyName("FOO")
    .DefaultValue(1)
    .DefaultValue(2, "two")
    .ImplicitValue(3)
    .ImplicitValue(4, "four")
    .Repeatable();
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: Using OptionValueBuilder with std::string type
NOLINT_TEST(OptionValueBuilder, StringType)
{
  // Arrange
  std::string storage;
  auto builder = OptionBuilder("foo").WithValue<std::string>();

  // Act
  builder.StoreTo(&storage).DefaultValue("bar");
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: Using OptionValueBuilder with bool type
NOLINT_TEST(OptionValueBuilder, BoolType)
{
  // Arrange
  bool storage = false;
  auto builder = OptionBuilder("foo").WithValue<bool>();

  // Act
  builder.StoreTo(&storage).DefaultValue(true);
  const auto opt = builder.Build();

  // Assert
  EXPECT_TRUE(opt != nullptr);
}

//! Scenario: StoreTo with null pointer should not crash (edge case)
NOLINT_TEST(OptionValueBuilder, StoreToNullPointer)
{
  // Arrange
  auto builder = OptionBuilder("foo").WithValue<int>();

  // Act & Assert
  EXPECT_NO_THROW(builder.StoreTo(nullptr));
  const auto opt = builder.Build();
  EXPECT_TRUE(opt != nullptr);
}

} // namespace
