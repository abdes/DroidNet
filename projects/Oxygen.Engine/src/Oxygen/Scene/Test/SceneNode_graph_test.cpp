//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Graph/Hierarchy Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode graph/hierarchy scenarios.
class SceneNodeGraphTest : public SceneNodeTestBase { };

/*! Test parent-child relationship navigation and hierarchy queries.
    Scenario: Create parent and child, verify navigation and hierarchy flags. */
NOLINT_TEST_F(SceneNodeGraphTest, ParentChildRelationship_NavigationWorks)
{
  // Arrange: Create parent and child nodes
  auto parent = scene_->CreateNode("Parent");
  auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto& child = child_opt.value();

  // Act & Assert: Test parent navigation from child
  const auto child_parent = child.GetParent();
  ASSERT_TRUE(child_parent.has_value());
  EXPECT_EQ(child_parent->GetHandle(), parent.GetHandle());

  // Act & Assert: Test child navigation from parent
  const auto parent_first_child = parent.GetFirstChild();
  ASSERT_TRUE(parent_first_child.has_value());
  EXPECT_EQ(parent_first_child->GetHandle(), child.GetHandle());

  // Act & Assert: Test hierarchy queries
  EXPECT_TRUE(child.HasParent());
  EXPECT_FALSE(child.IsRoot());
  EXPECT_TRUE(parent.HasChildren());
  EXPECT_TRUE(parent.IsRoot());
}

/*! Test sibling relationship navigation.
    Scenario: Create multiple siblings and verify next/prev navigation. */
NOLINT_TEST_F(SceneNodeGraphTest, SiblingRelationships_NavigationWorks)
{
  // Arrange: Create parent with multiple children
  auto parent = scene_->CreateNode("Parent");
  auto child1_opt = scene_->CreateChildNode(parent, "Child1");
  auto child2_opt = scene_->CreateChildNode(parent, "Child2");
  auto child3_opt = scene_->CreateChildNode(parent, "Child3");

  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  ASSERT_TRUE(child3_opt.has_value());

  // Act: Get first child and navigate through siblings
  auto first_child = parent.GetFirstChild();
  ASSERT_TRUE(first_child.has_value());

  auto next_sibling = first_child->GetNextSibling();
  ASSERT_TRUE(next_sibling.has_value());

  auto third_sibling = next_sibling->GetNextSibling();
  ASSERT_TRUE(third_sibling.has_value());

  // Act: Navigate back using previous sibling
  auto prev_sibling = third_sibling->GetPrevSibling();
  ASSERT_TRUE(prev_sibling.has_value());

  // Assert: Sibling navigation should be consistent
  EXPECT_EQ(prev_sibling->GetHandle(), next_sibling->GetHandle());
}

//! Test root node properties and navigation.
NOLINT_TEST_F(SceneNodeGraphTest, RootNode_BehavesCorrectly)
{
  // Arrange: Create a root node
  auto root = scene_->CreateNode("Root");

  // Act & Assert: Root node should have expected properties
  EXPECT_TRUE(root.IsRoot());
  EXPECT_FALSE(root.HasParent());
  EXPECT_FALSE(root.HasChildren());

  // Act & Assert: Navigation should return empty optional
  EXPECT_FALSE(root.GetParent().has_value());
  EXPECT_FALSE(root.GetFirstChild().has_value());
  EXPECT_FALSE(root.GetNextSibling().has_value());
  EXPECT_FALSE(root.GetPrevSibling().has_value());
}

/*! Test navigation and queries with invalid nodes.
    Scenario: Destroy a node and verify navigation returns empty/false. */
NOLINT_TEST_F(SceneNodeGraphTest, Navigation_WithInvalidNodes_ReturnsEmpty)
{
  // Arrange: Create a node then destroy it
  auto node = scene_->CreateNode("TestNode");
  scene_->DestroyNode(node);

  // Act & Assert: Navigation should return empty optional for invalid nodes
  EXPECT_FALSE(node.GetParent().has_value());
  EXPECT_FALSE(node.GetFirstChild().has_value());
  EXPECT_FALSE(node.GetNextSibling().has_value());
  EXPECT_FALSE(node.GetPrevSibling().has_value());

  // Act & Assert: Hierarchy queries should be false for invalid nodes
  EXPECT_FALSE(node.HasParent());
  EXPECT_FALSE(node.HasChildren());
  EXPECT_TRUE(node.IsRoot()); // invalid parent means no parent, so it is root
}

/*! Test that multiple handles to the same node share underlying data.
    Scenario: Get two handles to the same node and verify identity. */
NOLINT_TEST_F(SceneNodeGraphTest, MultipleHandlesToSameNode_ShareUnderlyingData)
{
  // Arrange: Create node and get second handle to same node
  auto node1 = scene_->CreateNode("TestNode");
  const auto handle = node1.GetHandle();
  const auto node2_opt = scene_->GetNode(handle);

  ASSERT_TRUE(node2_opt.has_value());
  auto node2 = node2_opt.value();

  // Act & Assert: Handles should be identical
  EXPECT_EQ(node1.GetHandle(), node2.GetHandle());

  // Act: Get implementations from both handles
  const auto impl1 = node1.GetObject();
  const auto impl2 = node2.GetObject();

  // Assert: Both should access the same underlying data
  ASSERT_TRUE(impl1.has_value());
  ASSERT_TRUE(impl2.has_value());
  EXPECT_EQ(&impl1->get(), &impl2->get());
}

} // namespace
