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

  // Helper to create a ConstVisitedNode for testing ADL functions
  [[nodiscard]] ConstVisitedNode CreateConstVisitedNode(
    std::size_t depth = 0) const
  {
    // Create ConstVisitedNode manually without using SceneQuery
    // Get a valid node handle from the scene for testing
    auto root_handle = scene_->GetRootNodes()[0].GetHandle();

    ConstVisitedNode visited;
    visited.handle = root_handle;
    visited.node_impl = &(scene_->GetNodeImplRefUnsafe(root_handle));
    visited.depth = depth;

    // Note: We ignore node_name parameter since we're testing ADL behavior
    // not the specific node name matching
    return visited;
  }

  // Helper to create invalid ConstVisitedNode for testing error handling
  [[nodiscard]] ConstVisitedNode CreateInvalidConstVisitedNode(
    std::size_t depth = 0) const
  {
    ConstVisitedNode invalid;
    invalid.handle = NodeHandle {};
    invalid.node_impl = nullptr;
    invalid.depth = depth;
    return invalid;
  }
};

//=== GetNodeName Tests ===================================================//

NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithValidNode_ReturnsName)
{
  // Arrange: Create a ConstVisitedNode directly for testing
  auto visited = CreateConstVisitedNode(0);

  // Act: Use ADL GetNodeName function
  auto name = GetNodeName(visited);

  // Assert: Should return a valid name (actual name depends on scene setup)
  EXPECT_FALSE(name.empty());
  // Note: We test the function works, not the specific name since we're testing
  // ADL behavior
}

NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithNullNodeImpl_ReturnsEmpty)
{
  // Arrange: Create ConstVisitedNode with null implementation
  auto visited = CreateInvalidConstVisitedNode();

  // Act: Use ADL GetNodeName function with null impl
  auto name = GetNodeName(visited);

  // Assert: Should return empty string
  EXPECT_TRUE(name.empty());
  EXPECT_EQ(name.size(), 0);
}

//=== GetDepth Tests ======================================================//

NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithRootNode_ReturnsZero)
{
  // Arrange: Create a ConstVisitedNode with depth 0
  auto visited = CreateConstVisitedNode();

  // Act: Use ADL GetDepth function
  auto depth = GetDepth(visited);

  // Assert: Should return the depth we set (0)
  EXPECT_EQ(depth, 0);
}

NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithNestedNode_ReturnsCorrectDepth)
{
  // Arrange: Create ConstVisitedNodes with different depths
  auto root_visited = CreateConstVisitedNode(1);
  auto child_visited = CreateConstVisitedNode(2);
  auto gchild_visited = CreateConstVisitedNode(3);

  // Act: Get depths using ADL function
  auto root_depth = GetDepth(root_visited);
  auto child_depth = GetDepth(child_visited);
  auto gchild_depth = GetDepth(gchild_visited);

  // Assert: Should return the depths we set
  EXPECT_EQ(root_depth, 1);
  EXPECT_EQ(child_depth, 2);
  EXPECT_EQ(gchild_depth, 3);

  // Verify depth increases with nesting
  EXPECT_LT(root_depth, child_depth);
  EXPECT_LT(child_depth, gchild_depth);
}

NOLINT_TEST_F(SceneQueryADLTest, ADLFunctions_ErrorHandling_WithInvalidData)
{
  // Arrange: Test ADL functions with various invalid inputs

  // Test with completely invalid ConstVisitedNode
  auto invalid_visited = CreateInvalidConstVisitedNode(0);

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
