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
#include <Oxygen/Scene/SceneTraversal.h>

#include "./Scene_traversal_test.h"

using oxygen::scene::TraversalOrder;
using oxygen::scene::VisitResult;

using oxygen::scene::testing::SceneTraversalBasicTest;

namespace {

//==============================================================================
// Visitor Control Tests
//
//     root
//    /    \
//   A      B
//  / \    /
// C   D  E
//==============================================================================

// Value-parameterized test suite for traversal order dependent tests
class SceneTraversalVisitorTest
  : public SceneTraversalBasicTest,
    public ::testing::WithParamInterface<TraversalOrder> { };

// TraversalOrderComparison as a parameterized test
NOLINT_TEST_P(SceneTraversalVisitorTest, FullTraversal)
{
  // Act: Traverse using post-order (children-first) traversal
  const auto result = traversal_->Traverse(CreateTrackingVisitor(), GetParam());

  // Assert: All nodes should be visited with no filtering
  CHECK_FOR_FAILURES_MSG(ExpectTraversalResult(result, GetNodeCount(), 0, true),
    "All nodes should be visited");

  // Verify complete semantic ordering using enhanced helper
  CHECK_FOR_FAILURES_MSG(
    ExpectSemanticOrdering(GetParam()), "Semantic ordering must be correct");
}

// EarlyTerminationAllOrders as a parameterized test
NOLINT_TEST_P(SceneTraversalVisitorTest, EarlyTermination)
{
  // Clear previous test state
  visit_order_.clear();
  visited_nodes_.clear();

  // Act: Stop traversal when we reach node "A"
  const auto result
    = traversal_->Traverse(CreateEarlyTerminationVisitor("A"), GetParam());

  // Assert: Should have stopped at "A"
  EXPECT_FALSE(result.completed)
    << "Should not complete due to early termination";
  EXPECT_EQ(visit_order_.back(), "A") << "Should terminate at node A";

  // Assert: Should visit only the nodes up to and including "A"
  // (order-dependent)
  std::vector<std::string> expected_nodes;
  switch (GetParam()) {
  case TraversalOrder::kPreOrder:
    expected_nodes = { "root", "A" };
    break;
  case TraversalOrder::kPostOrder:
    expected_nodes = { "A" };
    break;
  case TraversalOrder::kBreadthFirst:
    // "B" will be visited because it was added after "A", and will be traversed
    // before "A" is reached
    expected_nodes = { "root", "B", "A" };
    break;
  }
  CHECK_FOR_FAILURES_MSG(ExpectContainsExactlyNodes(expected_nodes),
    "Should visit only the expected nodes before early termination");
}

// SubtreeSkippingAllOrders as a parameterized test
NOLINT_TEST_P(SceneTraversalVisitorTest, SubtreeSkipping)
{
  // Clear previous test state
  visit_order_.clear();
  visited_nodes_.clear();

  // Act: Skip subtree of node "A" (should skip C and D)
  const auto result
    = traversal_->Traverse(CreateSubtreeSkippingVisitor("A"), GetParam());

  // Assert: Should complete but skip A's children
  CHECK_FOR_FAILURES_MSG(ExpectTraversalResult(result, 4, 0, true),
    "TraversalResult for subtree skipping");
  CHECK_FOR_FAILURES_MSG(
    ExpectContainsExactlyNodes({ "root", "A", "B", "E" }, { "C", "D" }),
    "Should visit A but not its children C and D");
}

INSTANTIATE_TEST_SUITE_P(AllOrders, SceneTraversalVisitorTest,
  ::testing::Values(TraversalOrder::kPreOrder, TraversalOrder::kPostOrder,
    TraversalOrder::kBreadthFirst),
  [](const ::testing::TestParamInfo<TraversalOrder>& info) {
    switch (info.param) {
    case TraversalOrder::kPreOrder:
      return "DFSPreOrder";
    case TraversalOrder::kPostOrder:
      return "DFSPostOrder";
    case TraversalOrder::kBreadthFirst:
      return "BreadthFirst";
    default:
      return "Unknown";
    }
  });

} // namespace
