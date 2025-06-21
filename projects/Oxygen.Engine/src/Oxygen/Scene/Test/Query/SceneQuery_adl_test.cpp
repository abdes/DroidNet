//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Testing/GTest.h>

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::NodeHandle;
using oxygen::scene::SceneNode;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== ADL Function Test Fixture ============================================//

class SceneQueryADLTest : public SceneQueryTestBase {
protected:
  auto SetUp() -> void override
  {
    // Create hierarchy suitable for ADL function testing
    CreateLinearChainScene(3);
  }

  // Helper to create a ConstVisitedNode for testing ADL functions
  [[nodiscard]] auto CreateConstVisitedNode(const std::size_t depth = 0) const
    -> ConstVisitedNode
  {
    // Create ConstVisitedNode manually without using SceneQuery
    // Get a valid node handle from the scene for testing
    const auto root_handle = scene_->GetRootNodes()[0].GetHandle();

    ConstVisitedNode visited;
    visited.handle = root_handle;
    visited.node_impl = &(scene_->GetNodeImplRefUnsafe(root_handle));
    visited.depth = depth;

    // Note: We ignore node_name parameter since we're testing ADL behavior
    // not the specific node name matching
    return visited;
  }

  // Helper to create invalid ConstVisitedNode for testing error handling
  [[nodiscard]] static auto CreateInvalidConstVisitedNode(
    const std::size_t depth = 0) -> ConstVisitedNode
  {
    ConstVisitedNode invalid;
    invalid.handle = NodeHandle {};
    invalid.node_impl = nullptr;
    invalid.depth = depth;
    return invalid;
  }
};

//=== GetNodeName Tests ===================================================//

//! Returns the node name for a valid ConstVisitedNode.
NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithValidNode_ReturnsName)
{
  // Arrange: Create a ConstVisitedNode directly for testing
  const auto visited = CreateConstVisitedNode(0);

  // Act: Use ADL GetNodeName function
  const auto name = GetNodeName(visited);

  // Assert: Should return a valid name (actual name depends on scene setup)
  EXPECT_FALSE(name.empty());
  // Note: We test the function works, not the specific name since we're testing
  // ADL behavior
}

//! Returns empty string when node_impl is null in ConstVisitedNode.
NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithNullNodeImpl_ReturnsEmpty)
{
  // Arrange: Create ConstVisitedNode with null implementation
  const auto visited = CreateInvalidConstVisitedNode();

  // Act: Use ADL GetNodeName function with null impl
  const auto name = GetNodeName(visited);

  // Assert: Should return empty string
  EXPECT_TRUE(name.empty());
  EXPECT_EQ(name.size(), 0);
}

//! GetNodeName returns empty for non-existent node handle.
NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithNonexistentNode_ReturnsEmpty)
{
  // Arrange: Create a ConstVisitedNode with a non-existent handle
  ConstVisitedNode visited;
  visited.handle = NodeHandle { 123456 }; // unlikely to exist
  visited.node_impl = nullptr;
  visited.depth = 0;

  // Act: Use ADL GetNodeName function
  const auto name = GetNodeName(visited);

  // Assert: Should return empty string
  EXPECT_TRUE(name.empty());
}

//=== GetDepth Tests ======================================================//

//! Returns the depth for a root node (should be zero).
NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithRootNode_ReturnsZero)
{
  // Arrange: Create a ConstVisitedNode with depth 0
  const auto visited = CreateConstVisitedNode();

  // Act: Use ADL GetDepth function
  const auto depth = GetDepth(visited);

  // Assert: Should return the depth we set (0)
  EXPECT_EQ(depth, 0);
}

//! Returns correct depth for nested nodes.
NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithNestedNode_ReturnsCorrectDepth)
{
  // Arrange: Create ConstVisitedNodes with different depths
  const auto root_visited = CreateConstVisitedNode(1);
  const auto child_visited = CreateConstVisitedNode(2);
  const auto grandchild_visited = CreateConstVisitedNode(3);

  // Act: Get depths using ADL function
  const auto root_depth = GetDepth(root_visited);
  const auto child_depth = GetDepth(child_visited);
  const auto grandchild_depth = GetDepth(grandchild_visited);

  // Assert: Should return the depths we set
  EXPECT_EQ(root_depth, 1);
  EXPECT_EQ(child_depth, 2);
  EXPECT_EQ(grandchild_depth, 3);

  // Verify depth increases with nesting
  EXPECT_LT(root_depth, child_depth);
  EXPECT_LT(child_depth, grandchild_depth);
}

//! GetDepth returns 0 for non-existent node handle.
NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithNonexistentNode_ReturnsZero)
{
  // Arrange: Create a ConstVisitedNode with a non-existent handle
  ConstVisitedNode visited;
  visited.handle = NodeHandle { 123456 }; // unlikely to exist
  visited.node_impl = nullptr;
  visited.depth = 0;

  // Act: Use ADL GetDepth function
  const auto depth = GetDepth(visited);

  // Assert: Should return 0 (the depth field)
  EXPECT_EQ(depth, 0);
}

//! ADL functions handle invalid ConstVisitedNode data gracefully.
NOLINT_TEST_F(SceneQueryADLTest, ADLFunctions_ErrorHandling_WithInvalidData)
{
  // Arrange: Test ADL functions with various invalid inputs

  // Test with completely invalid ConstVisitedNode
  const auto invalid_visited = CreateInvalidConstVisitedNode(0);

  // Act & Assert: Functions should handle invalid data gracefully

  // GetNodeName with invalid data
  const auto name = GetNodeName(invalid_visited);
  EXPECT_TRUE(name.empty());

  // GetDepth with invalid data (should still return the depth field)
  const auto depth = GetDepth(invalid_visited);
  EXPECT_EQ(depth, 0);

  // Test with valid handle but null impl
  auto valid_visited = CreateConstVisitedNode(5);
  // Override to null impl for testing
  valid_visited.node_impl = nullptr;

  const auto partial_name = GetNodeName(valid_visited);
  const auto partial_depth = GetDepth(valid_visited);

  EXPECT_TRUE(partial_name.empty()); // Should be empty due to null impl
  EXPECT_EQ(partial_depth, 5); // Should return the depth field value
}

} // namespace
