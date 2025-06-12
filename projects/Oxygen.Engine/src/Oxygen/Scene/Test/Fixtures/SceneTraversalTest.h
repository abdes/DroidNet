//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Test/Fixtures/SceneTest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Aliases for gtest/gmock matchers to reduce clutter
using ::testing::Contains;
using ::testing::Not;
using ::testing::UnorderedElementsAreArray;

// Forward declarations for traversal types
using ::oxygen::scene::FilterResult;
using ::oxygen::scene::SceneNodeImpl;
using ::oxygen::scene::TraversalResult;
using ::oxygen::scene::VisitedNode;
using ::oxygen::scene::VisitResult;

namespace oxygen::scene::testing {

//=============================================================================
// Scene Traversal Test Infrastructure
//=============================================================================

/// Base fixture for all scene traversal tests. Provides specialized setup
/// and helper methods for traversal testing scenarios.
class SceneTraversalTest : public SceneTest {
protected:
  void SetUp() override
  {
    SceneTest::SetUp();
    traversal_ = std::make_unique<SceneTraversal>(*scene_);
  }

  void TearDown() override
  {
    traversal_.reset();
    visited_nodes_.clear();
    visit_order_.clear();
    SceneTest::TearDown();
  }

  //=== Traversal Infrastructure ===------------------------------------------//

  std::unique_ptr<SceneTraversal> traversal_;
  std::vector<SceneNodeImpl*> visited_nodes_;
  std::vector<std::string> visit_order_;

  //=== Visitor Creation Helpers ===------------------------------------------//

  /// Creates a visitor that tracks all visited nodes.
  auto CreateTrackingVisitor()
  {
    return [this](const VisitedNode& node, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(node.node_impl->GetName());
      }
      return VisitResult::kContinue;
    };
  }

  /// Creates a visitor that stops at a specific node.
  auto CreateEarlyTerminationVisitor(const std::string& stop_at_name)
  {
    return [this, stop_at_name](const VisitedNode& node, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(node.node_impl->GetName());
      }
      return node.node_impl->GetName() == stop_at_name ? VisitResult::kStop
                                                       : VisitResult::kContinue;
    };
  }

  /// Creates a visitor that skips subtrees of specific nodes.
  auto CreateSubtreeSkippingVisitor(const std::string& skip_subtree_of)
  {
    return [this, skip_subtree_of](const VisitedNode& node,
             const Scene& /*scene*/, bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(node.node_impl->GetName());
      }
      return node.node_impl->GetName() == skip_subtree_of
        ? VisitResult::kSkipSubtree
        : VisitResult::kContinue;
    };
  }

  /// Creates a visitor that counts nodes only.
  auto CreateCountingVisitor(std::size_t& count)
  {
    return [&count](const VisitedNode& /*node*/, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        ++count;
      }
      return VisitResult::kContinue;
    };
  }

  //=== Filter Creation Helpers ===-------------------------------------------//

  /// Creates a filter that rejects specific nodes by name.
  static auto CreateRejectFilter(const std::vector<std::string>& reject_names)
  {
    return [reject_names](const VisitedNode& visited_node,
             FilterResult /*parent_filter_result*/) -> FilterResult {
      if (visited_node.node_impl == nullptr) {
        return FilterResult::kReject;
      }
      for (const auto& name : reject_names) {
        if (visited_node.node_impl->GetName() == name) {
          return FilterResult::kReject;
        }
      }
      return FilterResult::kAccept;
    };
  }

  /// Creates a filter that rejects subtrees of specific nodes.
  static auto CreateRejectSubtreeFilter(
    const std::vector<std::string>& reject_subtree_names)
  {
    return [reject_subtree_names](const VisitedNode& visited_node,
             FilterResult /*parent_filter_result*/) -> FilterResult {
      if (visited_node.node_impl == nullptr) {
        return FilterResult::kReject;
      }
      for (const auto& name : reject_subtree_names) {
        if (visited_node.node_impl->GetName() == name) {
          return FilterResult::kRejectSubTree;
        }
      }
      return FilterResult::kAccept;
    };
  }

  /// Creates a filter that only accepts visible nodes.
  static auto CreateVisibilityFilter()
  {
    return [](const VisitedNode& visited_node,
             FilterResult /*parent_filter_result*/) -> FilterResult {
      if (visited_node.node_impl == nullptr) {
        return FilterResult::kReject;
      }
      const auto& flags = visited_node.node_impl->GetFlags();
      return flags.GetEffectiveValue(SceneNodeFlags::kVisible)
        ? FilterResult::kAccept
        : FilterResult::kReject;
    };
  }

  //=== Traversal Assertion Helpers ===---------------------------------------//

  /// Validates that the expected nodes were visited in order.
  void ExpectVisitedNodes(const std::vector<std::string>& expected_names) const
  {
    EXPECT_EQ(visit_order_.size(), expected_names.size())
      << "Visited node count mismatch";
    EXPECT_EQ(visit_order_, expected_names) << "Visit order mismatch";
  }

  /// Validates traversal results.
  static void ExpectTraversalResult(const TraversalResult& result,
    const std::size_t expected_visited, const std::size_t expected_filtered,
    const bool expected_completed = true)
  {
    EXPECT_EQ(result.nodes_visited, expected_visited)
      << "Unexpected number of nodes visited";
    EXPECT_EQ(result.nodes_filtered, expected_filtered)
      << "Unexpected number of nodes filtered";
    EXPECT_EQ(result.completed, expected_completed)
      << "Unexpected completion status";
  }

  /// Validates that all expected nodes are present (order-independent).
  void ExpectContainsAllNodes(
    const std::vector<std::string>& expected_nodes) const
  {
    EXPECT_THAT(visit_order_, UnorderedElementsAreArray(expected_nodes))
      << "Expected nodes not found in visit order";
  }

  /// Validates that no nodes were visited.
  void ExpectNoNodesVisited() const
  {
    EXPECT_TRUE(visit_order_.empty()) << "Expected no nodes to be visited";
    EXPECT_TRUE(visited_nodes_.empty()) << "Expected no nodes to be tracked";
  }

  /// Validates that a specific node was visited.
  void ExpectNodeVisited(const std::string& node_name) const
  {
    EXPECT_THAT(visit_order_, Contains(node_name))
      << "Expected node '" << node_name << "' to be visited";
  }

  /// Validates that a specific node was not visited.
  void ExpectNodeNotVisited(const std::string& node_name) const
  {
    EXPECT_THAT(visit_order_, Not(Contains(node_name)))
      << "Expected node '" << node_name << "' to NOT be visited";
  }

  //=== Common Traversal Setups ===-------------------------------------------//

  /// Standard test hierarchy: Root -> (A, B), A -> (C, D), B -> E
  struct StandardTraversalSetup {
    SceneNode root;
    SceneNode node_a;
    SceneNode node_b;
    SceneNode node_c;
    SceneNode node_d;
    SceneNode node_e;
  };

  /// Creates a standard traversal test hierarchy.
  [[nodiscard]] auto CreateStandardTraversalHierarchy() const
    -> StandardTraversalSetup
  {
    auto root = CreateNode("root");
    auto node_a_opt = CreateChildNode(root, "A");
    auto node_b_opt = CreateChildNode(root, "B");
    EXPECT_TRUE(node_a_opt.has_value());
    EXPECT_TRUE(node_b_opt.has_value());

    auto node_c_opt = CreateChildNode(node_a_opt.value(), "C");
    auto node_d_opt = CreateChildNode(node_a_opt.value(), "D");
    auto node_e_opt = CreateChildNode(node_b_opt.value(), "E");
    EXPECT_TRUE(node_c_opt.has_value());
    EXPECT_TRUE(node_d_opt.has_value());
    EXPECT_TRUE(node_e_opt.has_value());

    // Update transforms for clean state
    UpdateSingleNodeTransforms(root);
    UpdateSingleNodeTransforms(node_a_opt.value());
    UpdateSingleNodeTransforms(node_b_opt.value());
    UpdateSingleNodeTransforms(node_c_opt.value());
    UpdateSingleNodeTransforms(node_d_opt.value());
    UpdateSingleNodeTransforms(node_e_opt.value());

    return { std::move(root), std::move(node_a_opt.value()),
      std::move(node_b_opt.value()), std::move(node_c_opt.value()),
      std::move(node_d_opt.value()), std::move(node_e_opt.value()) };
  }

  /// Large hierarchy for performance testing.
  struct LargeTraversalSetup {
    SceneNode root;
    std::vector<SceneNode> children;
    std::vector<SceneNode> grandchildren;
  };

  /// Creates a large hierarchy for performance testing.
  [[nodiscard]] auto CreateLargeTraversalHierarchy(
    std::size_t num_children = 10,
    std::size_t num_grandchildren_per_child = 5) const -> LargeTraversalSetup
  {
    auto root = CreateNode("root");
    std::vector<SceneNode> children;
    std::vector<SceneNode> grandchildren;

    children.reserve(num_children);
    grandchildren.reserve(num_children * num_grandchildren_per_child);

    for (std::size_t i = 0; i < num_children; ++i) {
      auto child_name = "child_" + std::to_string(i);
      auto child_opt = CreateChildNode(root, child_name);
      EXPECT_TRUE(child_opt.has_value());
      children.push_back(std::move(child_opt.value()));

      for (std::size_t j = 0; j < num_grandchildren_per_child; ++j) {
        auto grandchild_name
          = "grandchild_" + std::to_string(i) + "_" + std::to_string(j);
        auto grandchild_opt = CreateChildNode(children.back(), grandchild_name);
        EXPECT_TRUE(grandchild_opt.has_value());
        grandchildren.push_back(std::move(grandchild_opt.value()));
      }
    }
    return { std::move(root), std::move(children), std::move(grandchildren) };
  }
};

//=== Categorized Traversal Test Fixtures ===------------------------------//

/// Base class for basic traversal functionality tests.
class SceneTraversalBasicTest : public SceneTraversalTest { };

/// Base class for traversal filter tests.
class SceneTraversalFilterTest : public SceneTraversalTest { };

/// Base class for traversal visitor tests.
class SceneTraversalVisitorTest : public SceneTraversalTest { };

/// Base class for traversal performance tests.
class SceneTraversalPerformanceTest : public SceneTraversalTest {
protected:
  void SetUp() override
  {
    // Use larger scene capacity for performance tests
    scene_ = std::make_shared<Scene>("TraversalPerformanceTestScene", 8192);
    traversal_ = std::make_unique<SceneTraversal>(*scene_);
  }
};

/// Base class for transform-related traversal tests.
class SceneTraversalTransformTest : public SceneTraversalTest { };

} // namespace oxygen::scene::testing
