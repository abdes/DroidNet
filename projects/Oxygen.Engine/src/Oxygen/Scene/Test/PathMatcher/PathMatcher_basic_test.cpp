//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/PathMatcher.h>

#include "./Fixtures/PathMatcherTest.h"

using oxygen::scene::detail::query::CaseInsensitiveMatcher;
using oxygen::scene::detail::query::CaseSensitiveMatcher;
using oxygen::scene::detail::query::ParsedPath;
using oxygen::scene::detail::query::ParsePath;
using oxygen::scene::detail::query::PathErrorInfo;
using oxygen::scene::detail::query::PathSegment;
using oxygen::scene::detail::query::testing::PathMatcherBasicTest;

namespace {

// -----------------------------------------------------------------------------
// PathSegment Tests
// -----------------------------------------------------------------------------

class PathSegementTest : public PathMatcherBasicTest { };

NOLINT_TEST_F(PathSegementTest, PathSegmentEqualityAndInequality)
{
  // Test equality: identical segments
  PathSegment segment1("TestSegment", 0, false, false);
  PathSegment segment2("TestSegment", 0, false, false);
  EXPECT_EQ(segment1, segment2);
  EXPECT_FALSE(segment1 != segment2);

  // Test inequality: different names
  PathSegment different_name("DifferentName", 0, false, false);
  EXPECT_NE(segment1, different_name);

  // Test inequality: different position
  PathSegment different_position("TestSegment", 5, false, false);
  EXPECT_NE(segment1, different_position);

  // Test inequality: different single wildcard flag
  PathSegment different_single("TestSegment", 0, true, false);
  EXPECT_NE(segment1, different_single);
  EXPECT_NE(segment1, different_single);

  // Test inequality: different recursive wildcard flag
  PathSegment different_recursive("TestSegment", 0, false, true);
  EXPECT_NE(segment1, different_recursive);

  // Test equality: all wildcards enabled
  PathSegment all_wildcards1("TestSegment", 0, true, true);
  PathSegment all_wildcards2("TestSegment", 0, true, true);
  EXPECT_EQ(all_wildcards1, all_wildcards2);
}

NOLINT_TEST_F(PathSegementTest, PathSegmentEdgeCases)
{
  // Test that empty names are valid
  NOLINT_EXPECT_NO_THROW(PathSegment(""));
  NOLINT_EXPECT_NO_THROW(PathSegment("", 0, false, false));

  // Test special characters
  PathSegment special1("Test/Segment\\With*Special", 0, false, false);
  PathSegment special2("Test/Segment\\With*Special", 0, false, false);
  EXPECT_EQ(special1, special2);
}

// -----------------------------------------------------------------------------
// ParsedPath Tests
// -----------------------------------------------------------------------------

class ParsedPathTest : public PathMatcherBasicTest { };

NOLINT_TEST_F(ParsedPathTest, SgementsCollectionAccessors)
{
  // Test default construction
  ParsedPath default_path;
  EXPECT_TRUE(default_path.IsEmpty());
  EXPECT_EQ(default_path.Size(), 0);
  EXPECT_TRUE(default_path
      .IsValid()); // Default construction is valid (no parsing errors)
  EXPECT_FALSE(default_path.has_wildcards);
  // Test path with regular segments
  ParsedPath regular_path {
    .segments = {
       PathSegment("World", 0, false, false),
       PathSegment("Player", 0, false, false),
       PathSegment("Equipment", 0, false, false),
    },
    .original_path = "World/Player/Equipment",
    .has_wildcards = false,
  };
  EXPECT_FALSE(regular_path.IsEmpty());
  EXPECT_EQ(regular_path.Size(), 3);
  EXPECT_TRUE(regular_path.IsValid());
  EXPECT_FALSE(regular_path.has_wildcards);

  // Test path with wildcards
  ParsedPath wildcard_path {
    .segments = {
      PathSegment("World", 0, false, false),
      PathSegment("*", 0, true, false),
      PathSegment("**", 0, false, true),
    },
    .original_path = "World/*/**",
    .has_wildcards = true,
  };
  EXPECT_FALSE(wildcard_path.IsEmpty());
  EXPECT_EQ(wildcard_path.Size(), 3);
  EXPECT_TRUE(wildcard_path.IsValid());
  EXPECT_TRUE(wildcard_path.has_wildcards);
}

// -----------------------------------------------------------------------------
// Name Matchers Tests
// -----------------------------------------------------------------------------

class NameMatchersTest : public PathMatcherBasicTest { };

NOLINT_TEST_F(NameMatchersTest, CaseSensitiveMatcher)
{
  CaseSensitiveMatcher matcher;

  // Test exact matches
  EXPECT_TRUE(matcher("World", "World"));
  EXPECT_TRUE(matcher("Player", "Player"));
  EXPECT_TRUE(matcher("", "")); // Empty strings

  // Test case sensitivity
  EXPECT_FALSE(matcher("World", "world"));
  EXPECT_FALSE(matcher("PLAYER", "player"));
  EXPECT_FALSE(matcher("Equipment", "EQUIPMENT"));

  // Test different strings
  EXPECT_FALSE(matcher("World", "Player"));
  EXPECT_FALSE(matcher("Equipment", "Weapon"));

  // Test special characters
  EXPECT_TRUE(matcher("Test/Path\\With*Special", "Test/Path\\With*Special"));
  EXPECT_FALSE(matcher("Test/Path", "test/path"));

  // Test unicode characters
  EXPECT_TRUE(matcher("测试", "测试"));
  EXPECT_FALSE(matcher("测试", "Test"));

  // Test length differences
  EXPECT_FALSE(matcher("Short", "LongerString"));
  EXPECT_FALSE(matcher("LongerString", "Short"));
}

NOLINT_TEST_F(NameMatchersTest, CaseInsensitiveMatcher)
{
  CaseInsensitiveMatcher matcher;

  // Test exact matches (same case)
  EXPECT_TRUE(matcher("World", "World"));
  EXPECT_TRUE(matcher("Player", "Player"));
  EXPECT_TRUE(matcher("", "")); // Empty strings

  // Test case insensitive matches
  EXPECT_TRUE(matcher("World", "world"));
  EXPECT_TRUE(matcher("world", "WORLD"));
  EXPECT_TRUE(matcher("PLAYER", "player"));
  EXPECT_TRUE(matcher("Equipment", "EQUIPMENT"));
  EXPECT_TRUE(matcher("MixedCase", "mixedcase"));
  EXPECT_TRUE(matcher("mixedCASE", "MIXEDcase"));

  // Test different strings (should still fail even with case insensitivity)
  EXPECT_FALSE(matcher("World", "Player"));
  EXPECT_FALSE(matcher("Equipment", "Weapon"));

  // Test special characters (case insensitive for letters only)
  EXPECT_TRUE(matcher("Test/Path\\With*Special", "test/path\\with*special"));
  EXPECT_TRUE(matcher("PATH/TO/FILE", "path/to/file"));

  // Test length differences
  EXPECT_FALSE(matcher("Short", "LongerString"));
  EXPECT_FALSE(matcher("LongerString", "Short"));

  // Test mixed case with special characters
  EXPECT_TRUE(matcher("Node_123", "node_123"));
  EXPECT_TRUE(matcher("ITEM-456", "item-456"));

  // Test edge cases with numbers and symbols
  EXPECT_TRUE(matcher("Test123", "test123"));
  EXPECT_TRUE(matcher("A1B2C3", "a1b2c3"));
  EXPECT_FALSE(matcher("123ABC", "123DEF"));
}

} // namespace
