//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Basic Construction and Handle Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode basic construction and handle scenarios.
class SceneNodeBasicTest : public SceneNodeTestBase { };

/*! Test that constructor creates a valid node handle.
    Scenario: Create a node and verify handle validity and type. */
NOLINT_TEST_F(SceneNodeBasicTest, Constructor_CreatesValidNodeHandle)
{
  // Arrange: Scene is ready (done in SetUp)

  // Act: Create a test node
  const auto node = scene_->CreateNode("TestNode");

  // Assert: Node should be valid with correct resource type
  EXPECT_TRUE(node.IsValid());
  EXPECT_EQ(node.GetHandle().ResourceType(), SceneNode::GetResourceType());
}

/*! Test that copy constructor preserves node handle.
    Scenario: Copy a node and verify handle identity. */
NOLINT_TEST_F(SceneNodeBasicTest, CopyConstructor_PreservesHandle)
{
  // Arrange: Create a test node
  const auto node1 = scene_->CreateNode("TestNode1");

  // Act: Copy construct new node
  const auto node1_copy(node1); // NOLINT(*-unnecessary-copy-in-initialization)

  // Assert: Copy should have same handle
  EXPECT_EQ(node1.GetHandle(), node1_copy.GetHandle());
}

/*! Test that copy assignment updates node handle.
    Scenario: Assign one node to another and verify handle update. */
NOLINT_TEST_F(SceneNodeBasicTest, CopyAssignment_UpdatesHandle)
{
  // Arrange: Create two different nodes
  const auto node1 = scene_->CreateNode("TestNode1");
  const auto node2 = scene_->CreateNode("TestNode2");

  // Act: Copy assign node2 to node1_copy
  auto node1_copy = node1;
  node1_copy = node2;

  // Assert: Assignment should update handle
  EXPECT_EQ(node2.GetHandle(), node1_copy.GetHandle());
  EXPECT_NE(node1.GetHandle(), node1_copy.GetHandle());
}

/*! Test that move constructor transfers node handle.
    Scenario: Move a node and verify handle transfer. */
NOLINT_TEST_F(SceneNodeBasicTest, MoveConstructor_TransfersHandle)
{
  // Arrange: Create a test node
  auto node1 = scene_->CreateNode("TestNode1");
  const auto expected_handle = node1.GetHandle();

  // Act: Move construct new node
  const auto node1_moved(std::move(node1));

  // Assert: Moved node should have the handle
  EXPECT_TRUE(node1_moved.IsValid());
  EXPECT_EQ(node1_moved.GetHandle(), expected_handle);
}

/*! Test that move assignment transfers node handle.
    Scenario: Move assign one node to another and verify handle transfer. */
NOLINT_TEST_F(SceneNodeBasicTest, MoveAssignment_TransfersHandle)
{
  // Arrange: Create two nodes
  auto node2 = scene_->CreateNode("TestNode2");
  auto node3 = scene_->CreateNode("TestNode3");
  const auto expected_handle = node2.GetHandle();

  // Act: Move assign node2 to node3
  node3 = std::move(node2);

  // Assert: Move assignment should transfer handle
  EXPECT_TRUE(node3.IsValid());
  EXPECT_EQ(node3.GetHandle(), expected_handle);
}

//------------------------------------------------------------------------------
// Implementation Object Access Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode implementation object access scenarios.
class SceneNodeImplObjectTest : public SceneNodeTestBase { };

/*! Test that GetObject returns a valid implementation for a valid node.
    Scenario: Create node and access its implementation object. */
NOLINT_TEST_F(SceneNodeImplObjectTest, GetObject_ReturnsValidImplementation)
{
  // Arrange: Create a test node
  auto node = scene_->CreateNode("TestNode");

  // Act: Get the underlying implementation
  const auto impl = node.GetObject();

  // Assert: Implementation should be accessible with the correct name
  ASSERT_TRUE(impl.has_value());
  EXPECT_EQ(impl->get().GetName(), "TestNode");
}

/*! Test that GetObject with valid node accesses implementation methods.
    Scenario: Create node and access implementation methods. */
NOLINT_TEST_F(
  SceneNodeImplObjectTest, GetObjectWithValidNode_AccessesImplementation)
{
  // Arrange: Create a valid test node
  auto node = scene_->CreateNode("TestNode");

  // Act: Get the implementation object
  const auto impl = node.GetObject();

  // Assert: Should access SceneNodeImpl methods correctly
  ASSERT_TRUE(impl.has_value());
  EXPECT_EQ(impl->get().GetName(), "TestNode");
  EXPECT_TRUE(impl->get().IsTransformDirty());
}

/*! Test that GetObject with invalid node returns empty optional.
    Scenario: Destroy node and verify GetObject returns empty. */
NOLINT_TEST_F(SceneNodeImplObjectTest, GetObjectWithInvalidNode_ReturnsEmpty)
{
  // Arrange: Create a node then destroy it to make it invalid
  auto node = scene_->CreateNode("TestNode");
  scene_->DestroyNode(node);

  // Act: Attempt to get object from invalid node
  const auto impl = node.GetObject();

  // Assert: Should return empty optional
  EXPECT_FALSE(impl.has_value());
}

//------------------------------------------------------------------------------
// Flags Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode flags scenarios.
class SceneNodeFlagsTest : public SceneNodeTestBase { };

/*! Test that GetFlags returns valid flags with default values.
    Scenario: Create node and verify default flag values. */
NOLINT_TEST_F(SceneNodeFlagsTest, GetFlags_ReturnsValidFlagsWithDefaults)
{
  // Arrange: Create a test node
  auto node = scene_->CreateNode("TestNode");

  // Act: Get node flags
  const auto flags = node.GetFlags();

  // Assert: Flags should be accessible with expected default values
  ASSERT_TRUE(flags.has_value());
  const auto& flag_ref = flags->get();
  EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

/*! Test that GetFlags with valid node accesses custom flags.
    Scenario: Create node with custom flags and verify values. */
NOLINT_TEST_F(SceneNodeFlagsTest, GetFlagsWithValidNode_AccessesCustomFlags)
{
  // Arrange: Create node with custom flags
  const auto custom_flags = SceneNode::Flags {}
                              .SetFlag(SceneNodeFlags::kVisible,
                                SceneFlag {}.SetEffectiveValueBit(false))
                              .SetFlag(SceneNodeFlags::kStatic,
                                SceneFlag {}.SetEffectiveValueBit(true));
  auto node = scene_->CreateNode("TestNode", custom_flags);

  // Act: Get the flags
  const auto flags = node.GetFlags();

  // Assert: Custom flags should be preserved
  ASSERT_TRUE(flags.has_value());
  const auto& flag_ref = flags->get();
  EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

/*! Test that GetFlags with invalid node returns empty optional.
    Scenario: Destroy node and verify GetFlags returns empty. */
NOLINT_TEST_F(SceneNodeFlagsTest, GetFlagsWithInvalidNode_ReturnsEmpty)
{
  // Arrange: Create a node then destroy it
  auto node = scene_->CreateNode("TestNode");
  scene_->DestroyNode(node);

  // Act: Attempt to get flags from invalid node
  const auto flags = node.GetFlags();

  // Assert: Should return empty optional
  EXPECT_FALSE(flags.has_value());
}

//------------------------------------------------------------------------------
// Lazy Invalidation and Scene Expiration Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode lifetime and invalidation scenarios.
class SceneNodeLifetimeTest : public SceneNodeTestBase { };

/*! Test that lazy invalidation handles destroyed nodes.
    Scenario: Destroy node and verify lazy invalidation. */
NOLINT_TEST_F(SceneNodeLifetimeTest, LazyInvalidation_HandlesDestroyedNodes)
{
  // Arrange: Create node and copy handle
  auto node = scene_->CreateNode("TestNode");
  auto node_copy = node;

  EXPECT_TRUE(node.IsValid());
  EXPECT_TRUE(node_copy.IsValid());

  // Act: Destroy the original node
  scene_->DestroyNode(node);

  // Act: First access should detect invalidity
  const auto impl = node_copy.GetObject();

  // Assert: Access should fail and return empty optional
  EXPECT_FALSE(impl.has_value());
}

/*! Test that nodes fail gracefully after scene expiration.
    Scenario: Destroy scene and verify node operations fail. */
NOLINT_TEST_F(SceneNodeLifetimeTest, SceneExpiration_NodesFailGracefully)
{
  // Arrange: Create a node in valid scene
  auto node = scene_->CreateNode("TestNode");
  EXPECT_TRUE(node.IsValid());

  // Act: Destroy the scene
  scene_.reset();

  // Act & Assert: Node operations should fail gracefully
  const auto impl = node.GetObject();
  EXPECT_FALSE(impl.has_value());

  const auto flags = node.GetFlags();
  EXPECT_FALSE(flags.has_value());

  // Act & Assert: Navigation should also fail gracefully
  EXPECT_FALSE(node.GetParent().has_value());
  EXPECT_FALSE(node.GetFirstChild().has_value());
}

/*! Test that nodes in an empty scene fail gracefully.
    Scenario: Clear scene and verify node becomes invalid. */
NOLINT_TEST_F(SceneNodeLifetimeTest, EmptyScene_NodesFailGracefully)
{
  // Arrange: Create node in valid scene
  auto node = scene_->CreateNode("TestNode");
  EXPECT_TRUE(node.IsValid());

  // Act: Clear the scene
  scene_->Clear();

  // Act: Try to access node after scene clear
  const auto impl = node.GetObject();

  // Assert: Node should now be invalid when accessed
  EXPECT_FALSE(impl.has_value());
}

/*! Test that hierarchical destruction invalidates all nodes.
    Scenario: Destroy parent hierarchy and verify all descendants are invalid.
 */
NOLINT_TEST_F(
  SceneNodeLifetimeTest, HierarchicalDestruction_InvalidatesAllNodes)
{
  // Arrange: Create parent-child hierarchy
  auto parent = scene_->CreateNode("Parent");
  auto child1_opt = scene_->CreateChildNode(parent, "Child1");
  auto child2_opt = scene_->CreateChildNode(parent, "Child2");

  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  auto& child1 = child1_opt.value();
  auto& child2 = child2_opt.value();
  ASSERT_TRUE(scene_->Contains(child1));
  ASSERT_TRUE(scene_->Contains(child2));

  // Act: Destroy parent hierarchy
  const auto destroy_result = scene_->DestroyNodeHierarchy(parent);

  // Assert: Destruction should succeed
  EXPECT_TRUE(destroy_result);

  // Assert: Root node should become invalid
  EXPECT_FALSE(parent.IsValid()); // Immediately invalidated

  // Assert: Descendants are lazily invalidated
  EXPECT_FALSE(child1.GetParent().has_value());
  EXPECT_FALSE(child1.IsValid());

  EXPECT_FALSE(child2.GetParent().has_value());
  EXPECT_FALSE(child2.IsValid());
}

} // namespace
