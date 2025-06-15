//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include <Oxygen/Scene/Detail/PathMatcher.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::detail::query::testing {

//! Test-specific traversal node structure for PathMatcher testing.
/*!
 Lightweight structure containing the information needed for path matching
 tests: node name and hierarchical depth. Used only in test scenarios.
*/
struct TraversalNode {
  std::string name; //!< Node name for pattern matching
  std::vector<std::string>::size_type
    depth; //!< Hierarchical depth (0 = root level)

  //! Constructs traversal node with name and depth.
  TraversalNode(
    std::string_view node_name, std::vector<std::string>::size_type node_depth)
    : name(node_name)
    , depth(node_depth)
  {
  }

  ~TraversalNode() noexcept = default;

  OXYGEN_DEFAULT_COPYABLE(TraversalNode)
  OXYGEN_DEFAULT_MOVABLE(TraversalNode)
};

//! ADL-compatible GetNodeName function for test TraversalNode
/*!
 Enables test TraversalNode to work with concept-based PathMatcher.
*/
inline [[nodiscard]] auto GetNodeName(const TraversalNode& node) noexcept
  -> std::string_view
{
  return node.name;
}

//! ADL-compatible GetDepth function for test TraversalNode
/*!
 Enables test TraversalNode to work with concept-based PathMatcher.
*/
inline [[nodiscard]] auto GetDepth(const TraversalNode& node) noexcept
  -> std::vector<std::string>::size_type
{
  return node.depth;
}

//! Builder for creating flat traversal node sequences for testing.
class FlatTraversalDataBuilder {
private:
  std::vector<TraversalNode> nodes_;
  std::vector<std::string>::size_type current_depth_ = 0;

public:
  //! Adds a root-level node.
  FlatTraversalDataBuilder& AddNode(std::string_view name)
  {
    nodes_.emplace_back(name, current_depth_);
    return *this;
  }

  //! Adds a child node at the next depth level.
  FlatTraversalDataBuilder& AddChild(std::string_view name)
  {
    ++current_depth_;
    nodes_.emplace_back(name, current_depth_);
    return *this;
  }

  //! Moves up one level in the hierarchy.
  FlatTraversalDataBuilder& Up()
  {
    if (current_depth_ > 0) {
      --current_depth_;
    }
    return *this;
  }

  //! Builds the final traversal node sequence.
  [[nodiscard]] auto Build() const -> std::vector<TraversalNode>
  {
    return nodes_;
  }
};

//! Base test fixture providing common helpers for PathMatcher testing.
class PathMatcherTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  //! Helper to create a simple linear hierarchy.
  [[nodiscard]] auto CreateLinearHierarchy(
    const std::vector<std::string>& names) -> std::vector<TraversalNode>
  {
    FlatTraversalDataBuilder builder;
    for (size_t i = 0; i < names.size(); ++i) {
      if (i == 0) {
        builder.AddNode(names[i]);
      } else {
        builder.AddChild(names[i]);
      }
    }
    return builder.Build();
  }
  //! Helper to verify complete match progression.
  template <typename MatcherType = CaseSensitiveMatcher>
  auto ExpectCompleteMatch(const PathMatcher<MatcherType>& matcher,
    const std::vector<TraversalNode>& nodes) -> void
  {
    PatternMatchState state;
    MatchResult final_result = MatchResult::kNoMatch;

    // Arrange & Act
    for (const auto& node : nodes) {
      auto result = matcher.Match(node, state);
      EXPECT_NE(result, MatchResult::kNoMatch)
        << "Failed to match node: " << node.name << " at depth " << node.depth;

      // Track the final result
      if (result == MatchResult::kCompleteMatch) {
        final_result = result;
        // Don't break - continue processing to ensure no failures
      } else if (final_result != MatchResult::kCompleteMatch) {
        final_result = result;
      }
    }

    // Assert
    EXPECT_EQ(final_result, MatchResult::kCompleteMatch)
      << "Pattern should be complete after all nodes";
  }

  //! Helper to verify partial match failure.
  template <typename MatcherType = CaseSensitiveMatcher>
  auto ExpectMatchFailsAt(const PathMatcher<MatcherType>& matcher,
    const std::vector<TraversalNode>& nodes, size_t fail_index) -> void
  {
    PatternMatchState state;

    // Arrange & Act - match nodes before failure point
    for (size_t i = 0; i < fail_index && i < nodes.size(); ++i) {
      auto result = matcher.Match(nodes[i], state);
      EXPECT_NE(result, MatchResult::kNoMatch)
        << "Unexpected failure at node " << i << ": " << nodes[i].name;
    }

    // Act & Assert - expect failure at specific point
    if (fail_index < nodes.size()) {
      auto result = matcher.Match(nodes[fail_index], state);
      EXPECT_EQ(result, MatchResult::kNoMatch)
        << "Expected failure at node " << fail_index << ": "
        << nodes[fail_index].name;
    }
  }
};

//! Test fixture for basic matching scenarios.
class PathMatcherBasicTest : public PathMatcherTest {
protected:
  void SetUp() override { PathMatcherTest::SetUp(); }
};

//! Test fixture for wildcard matching scenarios.
class PathMatcherWildcardTest : public PathMatcherTest {
protected:
  void SetUp() override { PathMatcherTest::SetUp(); }
};

//! Test fixture for state management scenarios.
class PathMatcherStateTest : public PathMatcherTest {
protected:
  void SetUp() override { PathMatcherTest::SetUp(); }
};

//! Test fixture for error and edge case scenarios.
class PathMatcherErrorTest : public PathMatcherTest {
protected:
  void SetUp() override { PathMatcherTest::SetUp(); }
};

//! Test fixture for case sensitivity scenarios.
class PathMatcherCaseTest : public PathMatcherTest {
protected:
  void SetUp() override { PathMatcherTest::SetUp(); }
};

} // namespace oxygen::scene::detail::query::testing
