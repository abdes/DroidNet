//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/OptionValuesMap.h>

using oxygen::clap::OptionValue;
using oxygen::clap::OptionValuesMap;

namespace {

//! Verifies storing and retrieving a single value for an option.
NOLINT_TEST(OptionValuesMapTest, StoreAndRetrieveSingleValue)
{
  // Arrange
  OptionValuesMap ovm;
  OptionValue value { std::make_any<int>(42), "42", false };

  // Act
  ovm.StoreValue("count", value);

  // Assert
  EXPECT_TRUE(ovm.HasOption("count"));
  EXPECT_EQ(ovm.OccurrencesOf("count"), 1u);
  EXPECT_EQ(std::any_cast<int>(ovm.ValuesOf("count")[0].Value()), 42);
}

//! Verifies storing multiple values for the same option accumulates them.
NOLINT_TEST(OptionValuesMapTest, StoreMultipleValuesForSameOption)
{
  // Arrange
  OptionValuesMap ovm;
  OptionValue v1 { std::make_any<std::string>("foo"), "foo", false };
  OptionValue v2 { std::make_any<std::string>("bar"), "bar", false };

  // Act
  ovm.StoreValue("file", v1);
  ovm.StoreValue("file", v2);

  // Assert
  EXPECT_TRUE(ovm.HasOption("file"));
  EXPECT_EQ(ovm.OccurrencesOf("file"), 2u);
  EXPECT_EQ(std::any_cast<std::string>(ovm.ValuesOf("file")[0].Value()), "foo");
  EXPECT_EQ(std::any_cast<std::string>(ovm.ValuesOf("file")[1].Value()), "bar");
}

//! Verifies HasOption and OccurrencesOf for missing options.
NOLINT_TEST(OptionValuesMapTest, MissingOptionReturnsFalseAndZero)
{
  // Arrange
  OptionValuesMap ovm;

  // Act & Assert
  EXPECT_FALSE(ovm.HasOption("not_present"));
  EXPECT_EQ(ovm.OccurrencesOf("not_present"), 0u);
}

//! Verifies ValuesOf throws for missing option.
NOLINT_TEST(OptionValuesMapTest, ValuesOfThrowsForMissingOption)
{
  // Arrange
  OptionValuesMap ovm;

  // Act & Assert
  NOLINT_EXPECT_THROW({ auto _ = ovm.ValuesOf("missing"); }, std::out_of_range);
}

//! Verifies storing and retrieving values for multiple distinct options.
NOLINT_TEST(OptionValuesMapTest, StoreAndRetrieveMultipleOptions)
{
  // Arrange
  OptionValuesMap ovm;
  OptionValue v1 { std::make_any<int>(1), "1", false };
  OptionValue v2 { std::make_any<int>(2), "2", false };
  OptionValue v3 { std::make_any<std::string>("alpha"), "alpha", false };

  // Act
  ovm.StoreValue("num", v1);
  ovm.StoreValue("num", v2);
  ovm.StoreValue("name", v3);

  // Assert
  EXPECT_TRUE(ovm.HasOption("num"));
  EXPECT_TRUE(ovm.HasOption("name"));
  EXPECT_EQ(ovm.OccurrencesOf("num"), 2u);
  EXPECT_EQ(ovm.OccurrencesOf("name"), 1u);
  EXPECT_EQ(std::any_cast<int>(ovm.ValuesOf("num")[0].Value()), 1);
  EXPECT_EQ(std::any_cast<int>(ovm.ValuesOf("num")[1].Value()), 2);
  EXPECT_EQ(
    std::any_cast<std::string>(ovm.ValuesOf("name")[0].Value()), "alpha");
}

//! Verifies that storing values for options with similar names does not
//! interfere.
NOLINT_TEST(OptionValuesMapTest, OptionNameIsolation)
{
  // Arrange
  OptionValuesMap ovm;
  OptionValue v1 { std::make_any<int>(7), "7", false };
  OptionValue v2 { std::make_any<int>(8), "8", false };

  // Act
  ovm.StoreValue("opt", v1);
  ovm.StoreValue("optX", v2);

  // Assert
  EXPECT_TRUE(ovm.HasOption("opt"));
  EXPECT_TRUE(ovm.HasOption("optX"));
  EXPECT_EQ(ovm.OccurrencesOf("opt"), 1u);
  EXPECT_EQ(ovm.OccurrencesOf("optX"), 1u);
  EXPECT_EQ(std::any_cast<int>(ovm.ValuesOf("opt")[0].Value()), 7);
  EXPECT_EQ(std::any_cast<int>(ovm.ValuesOf("optX")[0].Value()), 8);
}

//! Verifies storing and retrieving boolean values (flag scenario).
NOLINT_TEST(OptionValuesMapTest, StoreAndRetrieveBoolFlag)
{
  // Arrange
  OptionValuesMap ovm;
  OptionValue flag { std::make_any<bool>(true), "true", false };

  // Act
  ovm.StoreValue("verbose", flag);

  // Assert
  EXPECT_TRUE(ovm.HasOption("verbose"));
  EXPECT_EQ(ovm.OccurrencesOf("verbose"), 1u);
  EXPECT_TRUE(std::any_cast<bool>(ovm.ValuesOf("verbose")[0].Value()));
}

} // namespace
