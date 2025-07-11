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

using oxygen::scene::AcceptAllFilter;
using oxygen::scene::TraversalOrder;
using oxygen::scene::VisitResult;

using oxygen::scene::testing::SceneTraversalBasicTest;

namespace {

//==============================================================================
// Filter Control Tests
//
//     root
//    /    \
//   A      B
//  / \    /
// C   D  E
//==============================================================================

// Value-parameterized test suite for traversal order dependent tests
class SceneTraversalFilterTest
  : public SceneTraversalBasicTest,
    public ::testing::WithParamInterface<TraversalOrder> { };

//! Tests that AcceptAllFilter visits all nodes (no filtering).
TEST_P(SceneTraversalFilterTest, AcceptAllFilter)
{
  // Act: Traverse with AcceptAllFilter
  const auto result = GetTraversal().Traverse(
    CreateTrackingVisitor(), TraversalOrder::kPreOrder, AcceptAllFilter {});
  // Assert: All nodes should be visited
  TRACE_GCHECK_F(ExpectTraversalResult(result, 6, 0, true), "accept-result");
  TRACE_GCHECK_F(ExpectContainsAllNodes({ "root", "A", "B", "C", "D", "E" }),
    "accept-nodes");
}

//! Tests that rejecting specific nodes with a filter excludes them but still
//! visits their children.
TEST_P(SceneTraversalFilterTest, RejectSpecificNodes)
{
  // Act: Traverse rejecting nodes A and E
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor(),
    TraversalOrder::kPreOrder, CreateRejectFilter({ "A", "E" }));
  // Assert: A and E should be filtered out but their children still visited
  TRACE_GCHECK_F(ExpectTraversalResult(result, 4, 2, true), "reject-result");
  TRACE_GCHECK_F(
    ExpectContainsExactlyNodes({ "root", "B", "C", "D" }, { "A", "E" }),
    "reject-nodes");
}

//! Tests that rejecting a subtree with a filter excludes the node and all its
//! descendants.
TEST_P(SceneTraversalFilterTest, RejectSubtreeOfSpecificNodes)
{
  // Act: Traverse rejecting subtree of node A
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor(),
    TraversalOrder::kPreOrder, CreateRejectSubtreeFilter({ "A" }));
  // Assert: A and its children (C, D) should be filtered out
  TRACE_GCHECK_F(ExpectTraversalResult(result, 3, 1, true), "subtree-result");
  TRACE_GCHECK_F(
    ExpectContainsExactlyNodes({ "root", "B", "E" }, { "A", "C", "D" }),
    "subtree-nodes");
}

//! Tests that rejecting a subtree in breadth-first traversal excludes the node
//! and all its descendants.
TEST_P(SceneTraversalFilterTest, RejectSubtreeInBreadthFirst)
{
  // Act: Traverse rejecting subtree of node B in breadth-first
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor(),
    TraversalOrder::kBreadthFirst, CreateRejectSubtreeFilter({ "B" }));
  // Assert: B and its children should be filtered out
  TRACE_GCHECK_F(ExpectTraversalResult(result, 4, 1, true), "breadth-result");
  TRACE_GCHECK_F(
    ExpectContainsExactlyNodes({ "root", "A", "C", "D" }, { "B", "E" }),
    "breadth-nodes");
}

INSTANTIATE_TEST_SUITE_P(AllOrders, SceneTraversalFilterTest,
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
