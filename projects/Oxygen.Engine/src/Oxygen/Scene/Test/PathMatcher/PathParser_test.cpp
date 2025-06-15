//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/PathParser.h>

#include "./Fixtures/PathParserTest.h"

using oxygen::scene::detail::query::ParsedPath;
using oxygen::scene::detail::query::PathParser;
using oxygen::scene::detail::query::PathSegment;

namespace {

// =============================================================================
// Test Fixtures
// =============================================================================

class PathParserBasicTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  // Helper methods for common operations
  ParsedPath ParsePath(std::string_view path)
  {
    PathParser parser(path);
    return parser.Parse();
  }

  void ExpectValidPath(const ParsedPath& result, size_t expected_segments)
  {
    EXPECT_TRUE(result.IsValid()) << "Path should be valid";
    EXPECT_FALSE(result.IsEmpty()) << "Path should not be empty";
    EXPECT_EQ(result.Size(), expected_segments) << "Unexpected segment count";
    EXPECT_FALSE(result.error_info.has_value()) << "Should not have error info";
  }

  void ExpectInvalidPath(
    const ParsedPath& result, const std::string& expected_error_substring = "")
  {
    EXPECT_FALSE(result.IsValid()) << "Path should be invalid";
    EXPECT_TRUE(result.error_info.has_value()) << "Should have error info";
    if (!expected_error_substring.empty() && result.error_info.has_value()) {
      EXPECT_THAT(result.error_info->error_message,
        ::testing::HasSubstr(expected_error_substring))
        << "Error message should contain expected substring";
    }
  }

  void ExpectSegment(const PathSegment& segment, const std::string& name,
    size_t position, bool is_single = false, bool is_recursive = false)
  {
    EXPECT_EQ(segment.name, name) << "Segment name mismatch";
    EXPECT_EQ(segment.start_position, position) << "Segment position mismatch";
    EXPECT_EQ(segment.is_wildcard_single, is_single)
      << "Single wildcard flag mismatch";
    EXPECT_EQ(segment.is_wildcard_recursive, is_recursive)
      << "Recursive wildcard flag mismatch";
  }
};

class PathParserErrorTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  void ExpectErrorAtPosition(const ParsedPath& result, size_t expected_position,
    const std::string& expected_error_substring)
  {
    EXPECT_FALSE(result.IsValid()) << "Path should be invalid";
    ASSERT_TRUE(result.error_info.has_value()) << "Should have error info";

    EXPECT_EQ(result.error_info->error_position, expected_position)
      << "Error position mismatch";
    EXPECT_THAT(result.error_info->error_message,
      ::testing::HasSubstr(expected_error_substring))
      << "Error message should contain expected substring";
  }
};

class PathParserDeathTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }
};

// =============================================================================
// Parameterized Test Data Structures
// =============================================================================

struct SlashHandlingTestCase {
  std::string_view path;
  std::vector<std::string> expected_segments;
  std::vector<size_t> expected_positions;
  std::string description;
};

struct WildcardSimplificationTestCase {
  std::string_view path;
  std::vector<std::string> expected_segments;
  std::vector<size_t> expected_positions;
  std::vector<bool> expected_single;
  std::vector<bool> expected_recursive;
  std::string description;
};

struct ErrorTestCase {
  std::string_view path;
  size_t expected_error_position;
  std::string expected_error_substring;
  std::string description;
};

struct EscapeSequenceTestCase {
  std::string_view path;
  std::vector<std::string> expected_segments;
  std::vector<size_t> expected_positions;
  std::string description;
};

struct PositionTrackingTestCase {
  std::string_view path;
  std::vector<std::string> expected_segments;
  std::vector<size_t> expected_positions;
  std::string description;
};

struct BasicFunctionalityTestCase {
  std::string_view path;
  std::vector<std::string> expected_segments;
  std::vector<size_t> expected_positions;
  bool should_have_wildcards;
  std::string description;
};

struct ApiCoverageTestCase {
  std::string_view path;
  bool should_be_valid;
  bool should_be_empty;
  size_t expected_size;
  std::string description;
};

// =============================================================================
// Parameterized Test Fixtures
// =============================================================================

class SlashHandlingParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<SlashHandlingTestCase> { };

class WildcardSimplificationParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<WildcardSimplificationTestCase> { };

class ErrorParameterizedTest
  : public PathParserErrorTest,
    public ::testing::WithParamInterface<ErrorTestCase> { };

class EscapeSequenceParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<EscapeSequenceTestCase> { };

class PositionTrackingParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<PositionTrackingTestCase> { };

class BasicFunctionalityParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<BasicFunctionalityTestCase> { };

class ApiCoverageParameterizedTest
  : public PathParserBasicTest,
    public ::testing::WithParamInterface<ApiCoverageTestCase> { };

// =============================================================================
// Edge Cases Tests
// =============================================================================

// =============================================================================
// API Coverage Tests
// =============================================================================

TEST_F(PathParserBasicTest, MultipleParseCalls_ReturnSameResult)
{
  // Arrange
  PathParser parser("World/Player/**/Equipment");

  // Act - Parse multiple times
  auto result1 = parser.Parse();
  auto result2 = parser.Parse();
  auto result3 = parser.Parse();

  // Assert - All results should be identical
  EXPECT_TRUE(result1.IsValid()) << "First parse should be valid";
  EXPECT_TRUE(result2.IsValid()) << "Second parse should be valid";
  EXPECT_TRUE(result3.IsValid()) << "Third parse should be valid";

  EXPECT_EQ(result1.original_path, result2.original_path);
  EXPECT_EQ(result1.original_path, result3.original_path);

  EXPECT_EQ(result1.has_wildcards, result2.has_wildcards);
  EXPECT_EQ(result1.has_wildcards, result3.has_wildcards);

  EXPECT_EQ(result1.segments.size(), result2.segments.size());
  EXPECT_EQ(result1.segments.size(), result3.segments.size());

  // Compare segments in detail
  for (size_t i = 0; i < result1.segments.size(); ++i) {
    EXPECT_EQ(result1.segments[i], result2.segments[i])
      << "Segment " << i
      << " should be identical between first and second parse";
    EXPECT_EQ(result1.segments[i], result3.segments[i])
      << "Segment " << i
      << " should be identical between first and third parse";
  }

  // Error info should be identical
  EXPECT_EQ(result1.error_info.has_value(), result2.error_info.has_value());
  EXPECT_EQ(result1.error_info.has_value(), result3.error_info.has_value());
}

TEST_F(PathParserErrorTest, MultipleParseCalls_ReturnSameErrorResult)
{
  // Arrange - Use a path that will cause an error
  PathParser parser("World/Player/\x01Invalid");

  // Act - Parse multiple times
  auto result1 = parser.Parse();
  auto result2 = parser.Parse();
  auto result3 = parser.Parse();

  // Assert - All results should be identical errors
  EXPECT_FALSE(result1.IsValid()) << "First parse should be invalid";
  EXPECT_FALSE(result2.IsValid()) << "Second parse should be invalid";
  EXPECT_FALSE(result3.IsValid()) << "Third parse should be invalid";

  EXPECT_EQ(result1.original_path, result2.original_path);
  EXPECT_EQ(result1.original_path, result3.original_path);

  // Error info should be identical
  ASSERT_TRUE(result1.error_info.has_value());
  ASSERT_TRUE(result2.error_info.has_value());
  ASSERT_TRUE(result3.error_info.has_value());

  EXPECT_EQ(
    result1.error_info->error_message, result2.error_info->error_message);
  EXPECT_EQ(
    result1.error_info->error_message, result3.error_info->error_message);

  EXPECT_EQ(
    result1.error_info->error_position, result2.error_info->error_position);
  EXPECT_EQ(
    result1.error_info->error_position, result3.error_info->error_position);
}

// =============================================================================
// Parameterized Tests
// =============================================================================

TEST_P(SlashHandlingParameterizedTest, HandlesSlashesCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);
  this->ExpectValidPath(result, test_case.expected_segments.size());

  for (size_t i = 0; i < test_case.expected_segments.size(); ++i) {
    this->ExpectSegment(result.segments[i], test_case.expected_segments[i],
      test_case.expected_positions[i]);
  }
}

TEST_P(WildcardSimplificationParameterizedTest, SimplifiesWildcardsCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);
  this->ExpectValidPath(result, test_case.expected_segments.size());
  EXPECT_TRUE(result.has_wildcards);

  for (size_t i = 0; i < test_case.expected_segments.size(); ++i) {
    this->ExpectSegment(result.segments[i], test_case.expected_segments[i],
      test_case.expected_positions[i], test_case.expected_single[i],
      test_case.expected_recursive[i]);
  }
}

TEST_P(ErrorParameterizedTest, ReportsErrorsCorrectly)
{
  auto test_case = GetParam();

  // Act
  PathParser parser(test_case.path);
  auto result = parser.Parse();

  // Assert
  SCOPED_TRACE(test_case.description);
  this->ExpectErrorAtPosition(result, test_case.expected_error_position,
    test_case.expected_error_substring);
}

TEST_P(EscapeSequenceParameterizedTest, HandlesEscapeSequencesCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);
  this->ExpectValidPath(result, test_case.expected_segments.size());

  for (size_t i = 0; i < test_case.expected_segments.size(); ++i) {
    this->ExpectSegment(result.segments[i], test_case.expected_segments[i],
      test_case.expected_positions[i]);
  }
}

TEST_P(PositionTrackingParameterizedTest, TracksPositionsCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);
  this->ExpectValidPath(result, test_case.expected_segments.size());

  for (size_t i = 0; i < test_case.expected_segments.size(); ++i) {
    this->ExpectSegment(result.segments[i], test_case.expected_segments[i],
      test_case.expected_positions[i]);
  }
}

TEST_P(BasicFunctionalityParameterizedTest, ParsesBasicFunctionalityCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);

  // Handle empty path case specially
  if (test_case.expected_segments.empty()) {
    EXPECT_TRUE(result.IsValid());
    EXPECT_TRUE(result.IsEmpty());
    EXPECT_EQ(result.Size(), 0);
    EXPECT_EQ(result.original_path, "");
    EXPECT_FALSE(result.has_wildcards);
  } else {
    this->ExpectValidPath(result, test_case.expected_segments.size());
    EXPECT_EQ(result.has_wildcards, test_case.should_have_wildcards);

    for (size_t i = 0; i < test_case.expected_segments.size(); ++i) {
      this->ExpectSegment(result.segments[i], test_case.expected_segments[i],
        test_case.expected_positions[i]);
    }
  }
}

TEST_P(ApiCoverageParameterizedTest, ApiMethodsWorkCorrectly)
{
  auto test_case = GetParam();

  // Act
  auto result = this->ParsePath(test_case.path);

  // Assert
  SCOPED_TRACE(test_case.description);
  EXPECT_EQ(result.IsValid(), test_case.should_be_valid);
  EXPECT_EQ(result.IsEmpty(), test_case.should_be_empty);
  EXPECT_EQ(result.Size(), test_case.expected_size);
}

// =============================================================================
// Test Data
// =============================================================================

INSTANTIATE_TEST_SUITE_P(SlashHandling, SlashHandlingParameterizedTest,
  ::testing::Values(
    SlashHandlingTestCase {
      .path = "///foo/bar",
      .expected_segments = { "", "", "", "foo", "bar" },
      .expected_positions = { 0, 1, 2, 3, 7 },
      .description = "Leading slashes create empty segments",
    },
    SlashHandlingTestCase {
      .path = "foo/bar///",
      .expected_segments = { "foo", "bar", "", "", "" },
      .expected_positions = { 0, 4, 8, 9, 10 },
      .description = "Trailing slashes create empty segments",
    },
    SlashHandlingTestCase {
      .path = "//foo/bar//",
      .expected_segments = { "", "", "foo", "bar", "", "" },
      .expected_positions = { 0, 1, 2, 6, 10, 11 },
      .description = "Leading and trailing slashes create empty segments",
    },
    SlashHandlingTestCase {
      .path = "////",
      .expected_segments = { "", "", "", "", "" },
      .expected_positions = { 0, 1, 2, 3, 4 },
      .description = "Only slashes create all empty segments",
    },
    SlashHandlingTestCase {
      .path = "/",
      .expected_segments = { "", "" },
      .expected_positions = { 0, 1 },
      .description = "Single slash creates two empty segments",
    },
    SlashHandlingTestCase {
      .path = "foo//bar",
      .expected_segments = { "foo", "", "bar" },
      .expected_positions = { 0, 4, 5 },
      .description = "Consecutive slashes in middle create empty segment",
    },
    SlashHandlingTestCase {
      .path = "foo///bar",
      .expected_segments = { "foo", "", "", "bar" },
      .expected_positions = { 0, 4, 5, 6 },
      .description
      = "Multiple consecutive slashes create multiple empty segments",
    },
    SlashHandlingTestCase {
      .path = "/foo/bar",
      .expected_segments = { "", "foo", "bar" },
      .expected_positions = { 0, 1, 5 },
      .description = "Leading slash creates empty segment at beginning",
    },
    SlashHandlingTestCase {
      .path = "foo/bar/",
      .expected_segments = { "foo", "bar", "" },
      .expected_positions = { 0, 4, 8 },
      .description = "Trailing slash creates empty segment at end",
    }));

INSTANTIATE_TEST_SUITE_P(WildcardSimplification,
  WildcardSimplificationParameterizedTest,
  ::testing::Values(
    WildcardSimplificationTestCase {
      .path = "*",
      .expected_segments = { "*" },
      .expected_positions = { 0 },
      .expected_single = { true },
      .expected_recursive = { false },
      .description = "Single wildcard remains unchanged",
    },
    WildcardSimplificationTestCase {
      .path = "**",
      .expected_segments = { "**" },
      .expected_positions = { 0 },
      .expected_single = { false },
      .expected_recursive = { true },
      .description = "Recursive wildcard remains unchanged",
    },
    WildcardSimplificationTestCase {
      .path = "foo/*/bar",
      .expected_segments = { "foo", "*", "bar" },
      .expected_positions = { 0, 4, 6 },
      .expected_single = { false, true, false },
      .expected_recursive = { false, false, false },
      .description = "Single wildcard in path",
    },
    WildcardSimplificationTestCase {
      .path = "foo/**/bar",
      .expected_segments = { "foo", "**", "bar" },
      .expected_positions = { 0, 4, 7 },
      .expected_single = { false, false, false },
      .expected_recursive = { false, true, false },
      .description = "Recursive wildcard in path",
    },
    WildcardSimplificationTestCase {
      .path = "**/**",
      .expected_segments = { "**" },
      .expected_positions = { 0 },
      .expected_single = { false },
      .expected_recursive = { true },
      .description = "Consecutive recursive wildcards are simplified",
    },
    WildcardSimplificationTestCase {
      .path = "foo/*/**",
      .expected_segments = { "foo", "**" },
      .expected_positions = { 0, 4 },
      .expected_single = { false, false },
      .expected_recursive = { false, true },
      .description = "Single followed by recursive simplifies to recursive",
    },
    WildcardSimplificationTestCase {
      .path = "**/*",
      .expected_segments = { "**" },
      .expected_positions = { 0 },
      .expected_single = { false },
      .expected_recursive = { true },
      .description = "Recursive absorbs following single wildcard",
    },
    WildcardSimplificationTestCase {
      .path = "*/**/*",
      .expected_segments = { "**" },
      .expected_positions = { 0 },
      .expected_single = { false },
      .expected_recursive = { true },
      .description = "Complex pattern with multiple rules applied",
    },
    WildcardSimplificationTestCase {
      .path = "foo/**/*/**/bar",
      .expected_segments = { "foo", "**", "bar" },
      .expected_positions = { 0, 4, 12 },
      .expected_single = { false, false, false },
      .expected_recursive = { false, true, false },
      .description = "Recursive absorbs single wildcards in path",
    },
    WildcardSimplificationTestCase {
      .path = "foo/*/**",
      .expected_segments = { "foo", "**" },
      .expected_positions = { 0, 4 },
      .expected_single = { false, false },
      .expected_recursive = { false, true },
      .description = "Single followed by recursive becomes recursive",
    },
    WildcardSimplificationTestCase {
      .path = "*/**/*",
      .expected_segments = { "**" },
      .expected_positions = { 0 },
      .expected_single = { false },
      .expected_recursive = { true },
      .description = "Only wildcards pattern simplifies to single recursive",
    },
    WildcardSimplificationTestCase {
      .path = "foo/*/**/*/**/bar/*/**/*",
      .expected_segments = { "foo", "**", "bar", "**" },
      .expected_positions = { 0, 4, 14, 18 },
      .expected_single = { false, false, false, false },
      .expected_recursive = { false, true, false, true },
      .description = "Complex wildcard pattern simplifies correctly",
    }));

INSTANTIATE_TEST_SUITE_P(ErrorCases, ErrorParameterizedTest,
  ::testing::Values(
    ErrorTestCase {
      .path = "foo/bar\x01/baz",
      .expected_error_position = 7,
      .expected_error_substring = "Invalid character",
      .description = "Control character reports error",
    },
    ErrorTestCase {
      .path = "foo/bar\x01/baz",
      .expected_error_position = 7,
      .expected_error_substring = "Invalid character",
      .description = "Invalid character reports error",
    },
    ErrorTestCase {
      .path = "foo/bar\n/baz",
      .expected_error_position = 7,
      .expected_error_substring = "Invalid character",
      .description = "Newline character reports error",
    },
    ErrorTestCase {
      .path = "foo/ba\x01r/baz",
      .expected_error_position = 6,
      .expected_error_substring = "Invalid character",
      .description = "Control character in middle of segment",
    },
    ErrorTestCase {
      .path = "foo/bar\\x/baz",
      .expected_error_position = 7,
      .expected_error_substring = "Invalid escape sequence",
      .description = "Invalid escape sequence reports error",
    },
    ErrorTestCase {
      .path = "foo/bar\\",
      .expected_error_position = 7,
      .expected_error_substring = "Unterminated escape sequence",
      .description = "Unterminated escape at end of path",
    }));

INSTANTIATE_TEST_SUITE_P(EscapeSequences, EscapeSequenceParameterizedTest,
  ::testing::Values(
    EscapeSequenceTestCase {
      .path = "foo/bar\\*/baz",
      .expected_segments = { "foo", "bar\\*", "baz" },
      .expected_positions = { 0, 4, 10 },
      .description = "Escaped star is treated as literal",
    },
    EscapeSequenceTestCase {
      .path = "foo/\\*/bar",
      .expected_segments = { "foo", "\\*", "bar" },
      .expected_positions = { 0, 4, 7 },
      .description = "Escaped star standalone segment",
    },
    EscapeSequenceTestCase {
      .path = "foo/bar\\*\\*/baz",
      .expected_segments = { "foo", "bar\\*\\*", "baz" },
      .expected_positions = { 0, 4, 12 },
      .description = "Escaped double star is treated as literal",
    },
    EscapeSequenceTestCase {
      .path = "foo/\\**/bar",
      .expected_segments = { "foo", "\\**", "bar" },
      .expected_positions = { 0, 4, 8 },
      .description = "Escaped double star standalone segment",
    },
    EscapeSequenceTestCase {
      .path = "foo/bar\\\\/baz",
      .expected_segments = { "foo", "bar\\\\", "baz" },
      .expected_positions = { 0, 4, 10 },
      .description = "Escaped backslash is treated as literal",
    },
    EscapeSequenceTestCase {
      .path = "foo/\\\\bar",
      .expected_segments = { "foo", "\\\\bar" },
      .expected_positions = { 0, 4 },
      .description = "Escaped backslash in segment",
    },
    EscapeSequenceTestCase {
      .path = "foo/bar\\/baz",
      .expected_segments = { "foo", "bar\\/baz" },
      .expected_positions = { 0, 4 },
      .description
      = "Escaped slash is treated as literal and doesn't split segment",
    }));

INSTANTIATE_TEST_SUITE_P(PositionTracking, PositionTrackingParameterizedTest,
  ::testing::Values(
    PositionTrackingTestCase {
      .path = "foo/bar/baz",
      .expected_segments = { "foo", "bar", "baz" },
      .expected_positions = { 0, 4, 8 },
      .description = "Simple path with correct positions",
    },
    PositionTrackingTestCase {
      .path = "//foo/bar",
      .expected_segments = { "", "", "foo", "bar" },
      .expected_positions = { 0, 1, 2, 6 },
      .description = "Leading slashes track positions correctly",
    },
    PositionTrackingTestCase {
      .path = "foo/bar\\*/baz",
      .expected_segments = { "foo", "bar\\*", "baz" },
      .expected_positions = { 0, 4, 10 },
      .description = "Escape sequences track positions correctly",
    },
    PositionTrackingTestCase {
      .path = "测试/файл/フォルダ",
      .expected_segments = { "测试", "файл", "フォルダ" },
      .expected_positions = { 0, 7, 16 },
      .description = "UTF-8 segments track byte positions correctly",
    },
    PositionTrackingTestCase {
      .path = "a/b/c/d/e/f/g",
      .expected_segments = { "a", "b", "c", "d", "e", "f", "g" },
      .expected_positions = { 0, 2, 4, 6, 8, 10, 12 },
      .description = "Multiple single-character segments",
    }));

INSTANTIATE_TEST_SUITE_P(BasicFunctionality,
  BasicFunctionalityParameterizedTest,
  ::testing::Values(
    BasicFunctionalityTestCase {
      .path = "",
      .expected_segments = {},
      .expected_positions = {},
      .should_have_wildcards = false,
      .description = "Empty path returns valid empty result",
    },
    BasicFunctionalityTestCase {
      .path = "segment",
      .expected_segments = { "segment" },
      .expected_positions = { 0 },
      .should_have_wildcards = false,
      .description = "Single segment parses correctly",
    },
    BasicFunctionalityTestCase {
      .path = "foo/bar/baz",
      .expected_segments = { "foo", "bar", "baz" },
      .expected_positions = { 0, 4, 8 },
      .should_have_wildcards = false,
      .description = "Multiple segments parse correctly",
    },
    BasicFunctionalityTestCase {
      .path = "segment0/segment1/segment2/segment3/segment4/segment5/segment6/"
              "segment7/segment8/segment9",
      .expected_segments = {
        "segment0", "segment1", "segment2", "segment3", "segment4",
        "segment5", "segment6", "segment7", "segment8", "segment9",
      },
      .expected_positions = { 0, 9, 18, 27, 36, 45, 54, 63, 72, 81 },
      .should_have_wildcards = false,
      .description = "Long path with many segments parses correctly",
    },
    // Edge but valid wildcard uses: *** and *a*
    BasicFunctionalityTestCase {
      .path = "***",
      .expected_segments = { "***" },
      .expected_positions = { 0 },
      .should_have_wildcards = false,
      .description
      = "Triple star is treated as a literal segment, not a wildcard",
    },
    BasicFunctionalityTestCase {
      .path = "*a*",
      .expected_segments = { "*a*" },
      .expected_positions = { 0 },
      .should_have_wildcards = false,
      .description
      = "Asterisk-surrounded segment is treated as a literal, not a wildcard",
    }));

INSTANTIATE_TEST_SUITE_P(ApiCoverage, ApiCoverageParameterizedTest,
  ::testing::Values(
    ApiCoverageTestCase {
      .path = "",
      .should_be_valid = true,
      .should_be_empty = true,
      .expected_size = 0,
      .description = "Empty path is valid, empty, and size 0",
    },
    ApiCoverageTestCase {
      .path = "foo",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 1,
      .description = "Single segment path is valid, not empty, and size 1",
    },
    ApiCoverageTestCase {
      .path = "foo/bar",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 2,
      .description = "Two segments path is valid, not empty, and size 2",
    },
    ApiCoverageTestCase {
      .path = "foo/bar/baz",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 3,
      .description = "Three segments path is valid, not empty, and size 3",
    },
    ApiCoverageTestCase {
      .path = "foo/bar\x01",
      .should_be_valid = false,
      .should_be_empty = false,
      .expected_size = 1,
      .description = "Path with invalid character is not valid, but returns "
                     "segments parsed before error",
    },
    ApiCoverageTestCase {
      .path = "foo/",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 2,
      .description = "Segment followed by slash creates empty segment (size 2)",
    },
    ApiCoverageTestCase {
      .path = "/foo",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 2,
      .description = "Leading slash creates empty segment (size 2)",
    },
    ApiCoverageTestCase {
      .path = "foo//bar",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 3,
      .description = "Consecutive slashes create empty segment (size 3)",
    },
    // Edge but valid wildcard uses: *** and *a*
    ApiCoverageTestCase {
      .path = "***",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 1,
      .description
      = "Triple star is treated as a literal segment, not a wildcard",
    },
    ApiCoverageTestCase {
      .path = "*a*",
      .should_be_valid = true,
      .should_be_empty = false,
      .expected_size = 1,
      .description
      = "Asterisk-surrounded segment is treated as a literal, not a wildcard",
    }));

} // namespace
