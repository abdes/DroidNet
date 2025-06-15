//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/PathMatcher.h>

using namespace oxygen::scene::detail::query;

namespace oxygen::scene::detail::query::testing {

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

} // namespace oxygen::scene::detail::query::testing
