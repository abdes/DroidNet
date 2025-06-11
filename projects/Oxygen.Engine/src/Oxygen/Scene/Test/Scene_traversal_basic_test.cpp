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
  CHECK_FOR_FAILURES_MSG(
    ExpectTraversalResult(result, 0, 0, true), "No nodes should be visited");
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
  CHECK_FOR_FAILURES_MSG(
    ExpectTraversalResult(result, 1, 0, true), "Single node should be visited");
  CHECK_FOR_FAILURES_MSG(
    ExpectVisitedNodes({ "single" }), "Single node should be visited");
}

} // namespace
