//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneTraversal.h>

#include "./Scene_traversal_test.h"
//
// using oxygen::scene::AcceptAllFilter;
// using oxygen::scene::DirtyTransformFilter;
// using oxygen::scene::FilterResult;
// using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
// using oxygen::scene::SceneFlag;
// using oxygen::scene::SceneNode;
// using oxygen::scene::SceneNodeFlags;
// using oxygen::scene::SceneNodeImpl;
// using oxygen::scene::SceneTraversal;
using oxygen::scene::TraversalOrder;
// using oxygen::scene::TraversalResult;
// using oxygen::scene::VisibleFilter;
using oxygen::scene::VisitedNode;
using oxygen::scene::VisitResult;

using oxygen::scene::testing::SceneTraversalBasicTest;

namespace {

//==============================================================================
// Basic Traversal Functionality Tests
//
//     root
//    /    \
//   A      B
//  / \    /
// C   D  E
//==============================================================================

//! Tests that traversing an empty scene visits no nodes.
NOLINT_TEST_F(SceneTraversalBasicTest, EmptySceneTraversal)
{
  // Arrange: Clear the scene to make it empty
  scene_->Clear();

  // Act: Traverse the empty scene
  const auto result = traversal_->Traverse(CreateTrackingVisitor());

  // Assert: No nodes should be visited
  ExpectTraversalResult(result, 0, 0, true);
  EXPECT_TRUE(visit_order_.empty());
}

//! Tests that traversing a scene with a single node visits only that node.
NOLINT_TEST_F(SceneTraversalBasicTest, SingleNodeTraversal)
{
  // Arrange: Clear scene and create a single node
  scene_->Clear();
  auto single_node = CreateNode("single");

  // Act: Traverse the scene with one node
  const auto result = traversal_->Traverse(CreateTrackingVisitor());

  // Assert: Single node should be visited
  ExpectTraversalResult(result, 1, 0, true);
  ExpectVisitedNodes({ "single" });
}

//! Tests that pre-order traversal visits all nodes (parent before children).
NOLINT_TEST_F(SceneTraversalBasicTest, PreOrderTraversal)
{
  // Act: Traverse using pre-order (parent-first) traversal
  const auto result
    = traversal_->Traverse(CreateTrackingVisitor(), TraversalOrder::kPreOrder);

  // Assert: All nodes should be visited with no filtering
  ExpectTraversalResult(result, 6, 0, true);

  // Verify all expected nodes are present (comprehensive validation)
  ExpectContainsExactlyNodes({ "root", "A", "B", "C", "D", "E" });

  // Pre-order semantic guarantee: parent must come before all its children
  EXPECT_EQ(visit_order_[0], "root")
    << "Root should be visited first in pre-order";

  // Verify complete semantic ordering using enhanced helper
  ExpectSemanticOrdering(TraversalOrder::kPreOrder);
}

//! Tests that breadth-first traversal visits all nodes level by level.
NOLINT_TEST_F(SceneTraversalBasicTest, BreadthFirstTraversal)
{
  // Act: Traverse using breadth-first order
  const auto result = traversal_->Traverse(
    CreateTrackingVisitor(), TraversalOrder::kBreadthFirst);

  // Assert: All nodes should be visited with no filtering
  ExpectTraversalResult(result, 6, 0, true);

  // Verify all expected nodes are present (comprehensive validation)
  ExpectContainsExactlyNodes({ "root", "A", "B", "C", "D", "E" });

  // Breadth-first semantic guarantee: level-by-level traversal
  EXPECT_EQ(visit_order_[0], "root") << "Root should be visited first";

  // Verify complete semantic ordering using enhanced helper
  ExpectSemanticOrdering(TraversalOrder::kBreadthFirst);
}

//! Tests that post-order traversal visits all nodes (children before parent).
NOLINT_TEST_F(SceneTraversalBasicTest, PostOrderTraversal)
{
  // Act: Traverse using post-order (children-first) traversal
  const auto result
    = traversal_->Traverse(CreateTrackingVisitor(), TraversalOrder::kPostOrder);

  // Assert: All nodes should be visited with no filtering
  ExpectTraversalResult(result, 6, 0, true);

  // Verify all expected nodes are present (comprehensive validation)
  ExpectContainsExactlyNodes({ "root", "A", "B", "C", "D", "E" });

  // Post-order semantic guarantee: children must come before their parent
  EXPECT_EQ(visit_order_.back(), "root")
    << "Root should be visited last in post-order";

  // Verify complete semantic ordering using enhanced helper
  ExpectSemanticOrdering(TraversalOrder::kPostOrder);
}

//! Tests that different traversal orders produce different visit sequences with
//! correct semantics.
NOLINT_TEST_F(SceneTraversalBasicTest, TraversalOrderComparison)
{
  // Arrange: Track visit orders for different traversal types
  std::vector<std::string> pre_order_visits;
  std::vector<std::string> post_order_visits;
  std::vector<std::string> breadth_first_visits;
  auto capture_visitor = [](std::vector<std::string>& visits_capture) {
    return [&visits_capture](const VisitedNode& node, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        visits_capture.emplace_back(node.node_impl->GetName());
      }
      return VisitResult::kContinue;
    };
  };

  // Act: Perform all three traversal types
  const auto pre_result = traversal_->Traverse(
    capture_visitor(pre_order_visits), TraversalOrder::kPreOrder);
  const auto post_result = traversal_->Traverse(
    capture_visitor(post_order_visits), TraversalOrder::kPostOrder);
  const auto breadth_result = traversal_->Traverse(
    capture_visitor(breadth_first_visits), TraversalOrder::kBreadthFirst);

  // Assert: All should visit the same nodes successfully
  ExpectTraversalResult(pre_result, 6, 0, true);
  ExpectTraversalResult(post_result, 6, 0, true);
  ExpectTraversalResult(breadth_result, 6, 0, true);

  // Assert: All sequences should contain exactly the same nodes
  const std::vector<std::string> expected_nodes
    = { "root", "A", "B", "C", "D", "E" };

  auto verify_contains_all
    = [&expected_nodes](
        const std::vector<std::string>& actual, const std::string& order_name) {
        EXPECT_EQ(actual.size(), 6)
          << order_name << " should visit exactly 6 nodes";
        for (const auto& expected : expected_nodes) {
          EXPECT_THAT(actual, ::testing::Contains(expected))
            << order_name << " should contain node: " << expected;
        }
      };

  verify_contains_all(pre_order_visits, "Pre-order");
  verify_contains_all(post_order_visits, "Post-order");
  verify_contains_all(breadth_first_visits, "Breadth-first");

  // Assert: Verify semantic ordering constraints are met
  EXPECT_EQ(pre_order_visits[0], "root") << "Pre-order should visit root first";
  EXPECT_EQ(post_order_visits.back(), "root")
    << "Post-order should visit root last";
  EXPECT_EQ(breadth_first_visits[0], "root")
    << "Breadth-first should visit root first";
  // Assert: The sequences should be different (confirm distinct ordering
  // semantics)
  EXPECT_NE(pre_order_visits, post_order_visits)
    << "Pre-order and post-order should produce different sequences";
  EXPECT_NE(pre_order_visits, breadth_first_visits)
    << "Pre-order and breadth-first should produce different sequences";
  EXPECT_NE(post_order_visits, breadth_first_visits)
    << "Post-order and breadth-first should produce different sequences";
}

//! Tests early termination behavior across all traversal orders.
NOLINT_TEST_F(SceneTraversalBasicTest, EarlyTerminationAllOrders)
{
  // Test early termination with each traversal order
  const std::vector<std::pair<TraversalOrder, std::string>> test_cases
    = { { TraversalOrder::kPreOrder, "Pre-order" },
        { TraversalOrder::kPostOrder, "Post-order" },
        { TraversalOrder::kBreadthFirst, "Breadth-first" } };

  for (const auto& [order, order_name] : test_cases) {
    // Clear previous test state
    visit_order_.clear();
    visited_nodes_.clear();

    // Act: Stop traversal when we reach node "A"
    const auto result
      = traversal_->Traverse(CreateEarlyTerminationVisitor("A"), order);

    // Assert: Should have stopped at "A", so nodes visited varies by order
    EXPECT_FALSE(result.completed)
      << order_name << " should not complete due to early termination";
    EXPECT_GT(result.nodes_visited, 0U)
      << order_name << " should visit at least one node";
    EXPECT_LE(result.nodes_visited, 6U)
      << order_name << " should not visit more than total nodes";

    // Assert: "A" should be the last visited node (termination point)
    EXPECT_EQ(visit_order_.back(), "A")
      << order_name << " should terminate at node A";

    // Assert: Should not visit nodes after termination
    ExpectContainsNoForbiddenNodes(
      { "C", "D" }); // A's children should not be visited after stopping at A
  }
}

//! Tests subtree skipping behavior across all traversal orders.
NOLINT_TEST_F(SceneTraversalBasicTest, SubtreeSkippingAllOrders)
{
  // Test subtree skipping with each traversal order
  const std::vector<std::pair<TraversalOrder, std::string>> test_cases
    = { { TraversalOrder::kPreOrder, "Pre-order" },
        { TraversalOrder::kPostOrder, "Post-order" },
        { TraversalOrder::kBreadthFirst, "Breadth-first" } };

  for (const auto& [order, order_name] : test_cases) {
    // Clear previous test state
    visit_order_.clear();
    visited_nodes_.clear();

    // Act: Skip subtree of node "A" (should skip C and D)
    const auto result
      = traversal_->Traverse(CreateSubtreeSkippingVisitor("A"), order);

    // Assert: Should complete but skip A's children
    ExpectTraversalResult(
      result, 4, 0, true); // root, A, B, E (C and D skipped)

    // Assert: Should visit A but not its children C and D
    ExpectContainsExactlyNodes({ "root", "A", "B", "E" }, { "C", "D" });
  }
}

} // namespace
