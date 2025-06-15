//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/*!
 PathMatcher matching behavior tests.

 This test suite verifies the hierarchical pattern matching capabilities of
 PathMatcher across different wildcard types, state management, and edge cases.
 Tests follow scenario-based naming and AAA (Arrange-Act-Assert) structure.

 Following TEST_INSTRUCTIONS.md guidelines:
 - Tests are organized by functionality using multiple fixtures
 - Each test checks one specific behavior or scenario
 - Helper methods are encapsulated in base fixture
 - Strong assertions with descriptive failure messages
*/

#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/PathMatcher.h>

#include "./Fixtures/PathMatcherTest.h"

using namespace oxygen::scene::detail::query;
using namespace oxygen::scene::detail::query::testing;

namespace {

// =============================================================================
// Basic Literal Matching Tests
// Tests exact string matching without wildcards for various hierarchy depths
// =============================================================================

NOLINT_TEST_F(PathMatcherBasicTest, SimpleLiteralPath_MatchesExactSequence)
{
  // Arrange
  PathMatcher matcher("foo/bar/baz");
  auto nodes = CreateLinearHierarchy({ "foo", "bar", "baz" });

  // Act & Assert
  TRACE_GCHECK_F(
    ExpectCompleteMatch(matcher, nodes), "Expected exact sequence match");
}

NOLINT_TEST_F(PathMatcherBasicTest, SingleSegmentPath_MatchesSingleNode)
{
  // Arrange
  PathMatcher matcher("root");
  auto nodes = CreateLinearHierarchy({ "root" });

  // Act & Assert
  TRACE_GCHECK_F(
    ExpectCompleteMatch(matcher, nodes), "Expected single segment match");
}

NOLINT_TEST_F(PathMatcherBasicTest, LiteralMismatch_FailsAtIncorrectSegment)
{
  // Arrange
  PathMatcher matcher("foo/bar/baz");
  auto nodes = CreateLinearHierarchy({ "foo", "wrong", "baz" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectMatchFailsAt(matcher, nodes, 1),
    "Expected failure at 'wrong' segment");
}

// =============================================================================
// Single Wildcard Matching Tests
// Tests single-level (*) wildcard behavior in various positions
// =============================================================================

NOLINT_TEST_F(PathMatcherWildcardTest, SingleWildcard_MatchesAnyNodeName)
{
  // Arrange
  PathMatcher matcher("foo/*/baz");
  auto nodes = CreateLinearHierarchy({ "foo", "anything", "baz" });
  // Act & Assert
  TRACE_GCHECK_F(
    ExpectCompleteMatch(matcher, nodes), "Expected single wildcard match");
}

NOLINT_TEST_F(PathMatcherWildcardTest, MultipleWildcards_MatchIndependently)
{
  // Arrange
  PathMatcher matcher("*/middle/*");
  auto nodes = CreateLinearHierarchy({ "start", "middle", "end" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected multiple wildcards to match");
}

NOLINT_TEST_F(PathMatcherWildcardTest, OnlySingleWildcard_MatchesSingleNode)
{
  // Arrange
  PathMatcher matcher("*");
  auto nodes = CreateLinearHierarchy({ "anything" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected single wildcard to match any node");
}

NOLINT_TEST_F(PathMatcherWildcardTest, RecursiveWildcard_MatchesDeepHierarchy)
{
  // Arrange
  PathMatcher matcher("start/**/end");
  auto nodes = FlatTraversalDataBuilder {}
                 .AddNode("start")
                 .AddChild("level1")
                 .AddChild("level2")
                 .AddChild("level3")
                 .AddChild("end")
                 .Build();

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected recursive wildcard to match deep hierarchy");
}

NOLINT_TEST_F(PathMatcherWildcardTest, RecursiveWildcard_MatchesZeroNodes)
{
  // Arrange
  PathMatcher matcher("start/**/end");
  auto nodes = CreateLinearHierarchy({ "start", "end" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected recursive wildcard to match zero nodes");
}

NOLINT_TEST_F(PathMatcherWildcardTest, OnlyRecursiveWildcard_MatchesEverything)
{
  // Arrange
  PathMatcher matcher("**");
  auto nodes = FlatTraversalDataBuilder {}
                 .AddNode("anything")
                 .AddChild("deep")
                 .AddChild("hierarchy")
                 .Up()
                 .AddChild("sibling")
                 .Build();

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected recursive wildcard to match everything");
}

NOLINT_TEST_F(
  PathMatcherWildcardTest, TrailingRecursiveWildcard_MatchesRemainder)
{
  // Arrange
  PathMatcher matcher("root/**");
  auto nodes = FlatTraversalDataBuilder {}
                 .AddNode("root")
                 .AddChild("anything")
                 .AddChild("goes")
                 .AddChild("here")
                 .Build();

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected trailing recursive wildcard to match remainder");
}

NOLINT_TEST_F(PathMatcherWildcardTest, MixedPattern_CombinesAllWildcardTypes)
{
  // Arrange
  PathMatcher matcher("*/data/**/file.txt");
  auto nodes = FlatTraversalDataBuilder {}
                 .AddNode("users") // matches *
                 .AddChild("data") // literal match
                 .AddChild("deep") // consumed by **
                 .AddChild("nested") // consumed by **
                 .AddChild("file.txt") // literal match
                 .Build();
  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected mixed pattern to combine all wildcard types");
}

// =============================================================================
// State Management Tests
// Tests pattern state reset, reuse, and partial matching behavior
// =============================================================================

NOLINT_TEST_F(PathMatcherStateTest, StateReset_AllowsReuse)
{
  // Arrange
  PathMatcher matcher("foo/*/baz");
  auto nodes = CreateLinearHierarchy({ "foo", "x", "baz" });
  PatternMatchState state;

  // Act - first complete match
  TRACE_GCHECK_F(
    ExpectCompleteMatch(matcher, nodes), "Expected first complete match");

  // Act - reset and match again
  state.Reset();

  // Assert
  EXPECT_TRUE(state.path_stack.empty())
    << "Path stack should be empty after reset";
  EXPECT_EQ(state.last_depth, -1) << "Last depth should reset to -1";

  // Act & Assert - can match again
  TRACE_GCHECK_F(
    ExpectCompleteMatch(matcher, nodes), "Expected reuse after reset to work");
}

NOLINT_TEST_F(PathMatcherStateTest, PartialMatch_PreservesState)
{
  // Arrange
  PathMatcher matcher("a/b/c/d");
  auto partial_nodes = CreateLinearHierarchy({ "a", "b" });
  PatternMatchState state;

  // Act - partial match
  MatchResult last_result = MatchResult::kNoMatch;
  for (const auto& node : partial_nodes) {
    last_result = matcher.Match(node, state);
    EXPECT_NE(last_result, MatchResult::kNoMatch);
  }

  // Assert - state reflects partial progress
  EXPECT_EQ(last_result, MatchResult::kPartialMatch);
  EXPECT_EQ(state.path_stack.size(), 2u)
    << "Should have 2 elements in path after matching 'a' and 'b'";
  EXPECT_EQ(state.path_stack[0], "a");
  EXPECT_EQ(state.path_stack[1], "b");
}

NOLINT_TEST_F(PathMatcherStateTest, CompletePattern_IgnoresAdditionalNodes)
{
  // Arrange
  PathMatcher matcher("a/b");
  auto nodes = CreateLinearHierarchy({ "a", "b", "extra" });
  PatternMatchState state;

  // Act - complete the pattern first
  auto result1 = matcher.Match(nodes[0], state);
  auto result2 = matcher.Match(nodes[1], state);
  EXPECT_NE(result1, MatchResult::kNoMatch);
  EXPECT_EQ(result2, MatchResult::kCompleteMatch);

  // Act & Assert - additional nodes extend beyond the pattern, so they should
  // be kNoMatch In streaming mode, "a/b/extra" is a different path than "a/b"
  auto result3 = matcher.Match(nodes[2], state);
  EXPECT_EQ(result3, MatchResult::kNoMatch)
    << "Additional nodes beyond complete pattern should return kNoMatch in "
       "streaming mode";

  // The path in state should still contain all processed nodes
  EXPECT_EQ(state.path_stack.size(), 3u)
    << "Path stack should contain all processed nodes";
  EXPECT_EQ(state.path_stack[0], "a");
  EXPECT_EQ(state.path_stack[1], "b");
  EXPECT_EQ(state.path_stack[2], "extra");
}

// =============================================================================
// Error Condition and Edge Case Tests
// Tests boundary conditions, literal stars, and malformed patterns
// =============================================================================

NOLINT_TEST_F(PathMatcherErrorTest, LiteralStars_NotTreatedAsWildcards)
{
  // Arrange
  PathMatcher matcher("foo/***/bar");
  auto nodes = CreateLinearHierarchy({ "foo", "***", "bar" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected literal stars to match exactly");
  EXPECT_FALSE(matcher.HasWildcards()) << "Literal *** should not be wildcard";
}

NOLINT_TEST_F(PathMatcherErrorTest, LiteralStarPattern_NotTreatedAsWildcard)
{
  // Arrange
  PathMatcher matcher("*a*");
  auto nodes = CreateLinearHierarchy({ "*a*" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, nodes),
    "Expected literal star pattern to match exactly");
  EXPECT_FALSE(matcher.HasWildcards()) << "Literal *a* should not be wildcard";
}

NOLINT_TEST_F(PathMatcherErrorTest, EmptyPattern_MatchesNothing)
{
  // Arrange
  PathMatcher matcher("");
  auto nodes = CreateLinearHierarchy({ "anything" });

  // Act & Assert
  PatternMatchState state;

  // Empty pattern should match empty path only, so any nodes should result in
  // kNoMatch
  auto result = matcher.Match(nodes[0], state);
  EXPECT_EQ(result, MatchResult::kNoMatch)
    << "Empty pattern should not match any nodes";

  // Path stack should still be updated even on no match
  EXPECT_EQ(state.path_stack.size(), 1u)
    << "Path stack should contain the processed node";
  EXPECT_EQ(state.path_stack[0], "anything");
}

// =============================================================================
// Case Sensitivity Tests
// Tests case-sensitive vs case-insensitive matching strategies
// =============================================================================

NOLINT_TEST_F(PathMatcherCaseTest, CaseSensitive_RequiresExactCase)
{
  // Arrange
  PathMatcher<CaseSensitiveMatcher> matcher("Foo/Bar");
  auto wrong_case_nodes = CreateLinearHierarchy({ "foo", "bar" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectMatchFailsAt(matcher, wrong_case_nodes, 0),
    "Expected case mismatch failure");
}

NOLINT_TEST_F(PathMatcherCaseTest, CaseInsensitive_IgnoresCase)
{
  // Arrange
  PathMatcher<CaseInsensitiveMatcher> matcher("FOO/bar");
  auto mixed_case_nodes = CreateLinearHierarchy({ "foo", "BAR" });

  // Act & Assert
  TRACE_GCHECK_F(ExpectCompleteMatch(matcher, mixed_case_nodes),
    "Expected case insensitive match");
}

} // namespace
