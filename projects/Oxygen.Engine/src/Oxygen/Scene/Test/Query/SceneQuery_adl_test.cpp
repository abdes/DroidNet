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
  void SetUp() override
  {
    // Create hierarchy suitable for ADL function testing
    CreateLinearChainScene(3);
  }

  // Helper to create a ConstVisitedNode for testing
  [[nodiscard]] ConstVisitedNode CreateConstVisitedNode(
    const SceneNode& node, std::size_t depth = 0) const
  {
    ConstVisitedNode visited;
    visited.handle = node.GetHandle();

    // Get the node implementation directly from the scene
    // Since we can't call GetObject() on const SceneNode (lazy invalidation),
    // we use the scene's method to get the implementation
    visited.node_impl = &(scene_->GetNodeImplRefUnsafe(node.GetHandle()));

    visited.depth = depth;
    return visited;
  }
};

//=== GetNodeName Tests ===================================================//

NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithValidNode_ReturnsName)
{
  // Arrange: Find a known node
  auto world_node = query_->FindFirst(NodeNameEquals("Root"));
  ASSERT_TRUE(world_node.has_value());

  auto visited = CreateConstVisitedNode(world_node.value());

  // Act: Use ADL GetNodeName function
  auto name = GetNodeName(visited);

  // Assert: Should return correct name
  EXPECT_EQ(name, "Root");
  EXPECT_FALSE(name.empty());
}

NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithNullNodeImpl_ReturnsEmpty)
{
  // Arrange: Create ConstVisitedNode with null implementation
  ConstVisitedNode visited;
  visited.handle = NodeHandle {}; // Invalid handle
  visited.node_impl = nullptr;
  visited.depth = 0;

  // Act: Use ADL GetNodeName function with null impl
  auto name = GetNodeName(visited);

  // Assert: Should return empty string
  EXPECT_TRUE(name.empty());
  EXPECT_EQ(name.size(), 0);
}

//=== GetDepth Tests ======================================================//

NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithRootNode_ReturnsZero)
{
  // Arrange: Get root node
  auto world_node = query_->FindFirst(NodeNameEquals("Root"));
  ASSERT_TRUE(world_node.has_value());

  auto visited = CreateConstVisitedNode(world_node.value(), 0);

  // Act: Use ADL GetDepth function
  auto depth = GetDepth(visited);

  // Assert: Root node should have depth 0
  EXPECT_EQ(depth, 0);
}

NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithNestedNode_ReturnsCorrectDepth)
{
  // Arrange: Get nodes at different depths
  auto root = query_->FindFirst(NodeNameEquals("Root")); // Depth 1
  auto child = query_->FindFirst(NodeNameEquals("Child")); // Depth 2
  auto grandchild = query_->FindFirst(NodeNameEquals("GrandChild")); // Depth 3

  ASSERT_TRUE(root.has_value());
  ASSERT_TRUE(child.has_value());
  ASSERT_TRUE(grandchild.has_value());

  // Create visited nodes with appropriate depths
  auto root_visited = CreateConstVisitedNode(root.value(), 1);
  auto child_visited = CreateConstVisitedNode(child.value(), 2);
  auto gchild_visited = CreateConstVisitedNode(grandchild.value(), 3);

  // Act: Get depths using ADL function
  auto root_depth = GetDepth(root_visited);
  auto child_depth = GetDepth(child_visited);
  auto gchild_depth = GetDepth(gchild_visited);

  // Assert: Should return correct depths
  EXPECT_EQ(root_depth, 1);
  EXPECT_EQ(child_depth, 2);
  EXPECT_EQ(gchild_depth, 3);

  // Verify depth increases with nesting
  EXPECT_LT(root_depth, child_depth);
  EXPECT_LT(child_depth, gchild_depth);
}

NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithZeroDepth_HandlesCorrectly)
{
  // Arrange: Test multiple root nodes (if any) with zero depth
  auto world = query_->FindFirst(NodeNameEquals("Root"));
  ASSERT_TRUE(world.has_value());

  auto visited_zero = CreateConstVisitedNode(world.value(), 0);

  // Act: Test zero depth handling
  auto depth = GetDepth(visited_zero);

  // Assert: Should handle zero depth correctly
  EXPECT_EQ(depth, 0);
  EXPECT_GE(depth, 0); // Depth should never be negative
}

NOLINT_TEST_F(SceneQueryADLTest, ADLFunctions_ErrorHandling_WithInvalidData)
{
  // Arrange: Test ADL functions with various invalid inputs

  // Test with completely invalid ConstVisitedNode
  constexpr ConstVisitedNode invalid_visited {
    .handle = NodeHandle {},
    .node_impl = nullptr,
    .depth = 0,
  };

  // Act & Assert: Functions should handle invalid data gracefully

  // GetNodeName with invalid data
  const auto name = GetNodeName(invalid_visited);
  EXPECT_TRUE(name.empty());

  // GetDepth with invalid data (should still return the depth field)
  const auto depth = GetDepth(invalid_visited);
  EXPECT_EQ(depth, 0);

  // Test with valid handle but null impl
  const auto valid_node = query_->FindFirst(NodeNameEquals("Root"));
  ASSERT_TRUE(valid_node.has_value());

  const ConstVisitedNode partial_invalid {
    .handle = valid_node->GetHandle(),
    .node_impl = nullptr, // Null implementation
    .depth = 5,
  };

  const auto partial_name = GetNodeName(partial_invalid);
  const auto partial_depth = GetDepth(partial_invalid);

  EXPECT_TRUE(partial_name.empty()); // Should be empty due to null impl
  EXPECT_EQ(partial_depth, 5); // Should return the depth field value
}

} // namespace
