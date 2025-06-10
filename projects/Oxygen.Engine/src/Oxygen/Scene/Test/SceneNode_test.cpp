//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::ObjectMetaData;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

//------------------------------------------------------------------------------
// Base Fixture for All SceneNode Tests
//------------------------------------------------------------------------------
class SceneNodeTestBase : public testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene", 1024); }
  void TearDown() override { scene_.reset(); }

  std::shared_ptr<Scene> scene_;
};

//------------------------------------------------------------------------------
// Basic Construction and Handle Tests
//------------------------------------------------------------------------------
class SceneNodeBasicTest : public SceneNodeTestBase { };

NOLINT_TEST_F(SceneNodeBasicTest, Constructor_CreatesValidNodeHandle)
{
  // Arrange: Scene is ready (done in SetUp)

  // Act: Create a test node
  const auto node = scene_->CreateNode("TestNode");

  // Assert: Node should be valid with correct resource type
  EXPECT_TRUE(node.IsValid());
  EXPECT_EQ(node.GetHandle().ResourceType(), oxygen::resources::kSceneNode);
}

NOLINT_TEST_F(SceneNodeBasicTest, CopyConstructor_PreservesHandle)
{
  // Arrange: Create a test node
  const auto node1 = scene_->CreateNode("TestNode1");

  // Act: Copy construct new node
  const auto node1_copy(node1); // NOLINT(*-unnecessary-copy-initialization)

  // Assert: Copy should have same handle
  EXPECT_EQ(node1.GetHandle(), node1_copy.GetHandle());
}

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
class SceneNodeImplObjectTest : public SceneNodeTestBase { };

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

NOLINT_TEST_F(
  SceneNodeImplObjectTest, GetObjectConst_ReturnsValidImplementation)
{
  // Arrange: Create a test node and get const reference
  const auto node = scene_->CreateNode("TestNode");
  const auto& const_node = node;

  // Act: Get implementation through const reference
  const auto impl = const_node.GetObject();

  // Assert: Const access should work correctly
  ASSERT_TRUE(impl.has_value());
  EXPECT_EQ(impl->get().GetName(), "TestNode");
}

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
class SceneNodeFlagsTest : public SceneNodeTestBase { };

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

NOLINT_TEST_F(SceneNodeFlagsTest, GetFlagsConst_ReturnsValidFlags)
{
  // Arrange: Create a test node and get const reference
  const auto node = scene_->CreateNode("TestNode");
  const auto& const_node = node;

  // Act: Get flags through const reference
  const auto flags = const_node.GetFlags();

  // Assert: Const flags access should work correctly
  ASSERT_TRUE(flags.has_value());
  const auto& flag_ref = flags->get();
  EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
}

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
// Graph/Hierarchy Tests
//------------------------------------------------------------------------------
class SceneNodeGraphTest : public SceneNodeTestBase { };

NOLINT_TEST_F(SceneNodeGraphTest, ParentChildRelationship_NavigationWorks)
{
  // Arrange: Create parent and child nodes
  const auto parent = scene_->CreateNode("Parent");
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  const auto& child = child_opt.value();

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

NOLINT_TEST_F(SceneNodeGraphTest, RootNode_BehavesCorrectly)
{
  // Arrange: Create a root node
  const auto root = scene_->CreateNode("Root");

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

//------------------------------------------------------------------------------
// Lazy Invalidation and Scene Expiration Tests
//------------------------------------------------------------------------------
class SceneNodeLifetimeTest : public SceneNodeTestBase { };

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

NOLINT_TEST_F(
  SceneNodeLifetimeTest, HierarchicalDestruction_InvalidatesAllNodes)
{
  // Arrange: Create parent-child hierarchy
  auto parent = scene_->CreateNode("Parent");
  const auto child1_opt = scene_->CreateChildNode(parent, "Child1");
  const auto child2_opt = scene_->CreateChildNode(parent, "Child2");

  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  const auto& child1 = child1_opt.value();
  const auto& child2 = child2_opt.value();
  ASSERT_TRUE(scene_->Contains(child1));
  ASSERT_TRUE(scene_->Contains(child2));

  // Act: Destroy parent hierarchy
  const auto destroy_result = scene_->DestroyNodeHierarchy(parent);

  // Assert: Destruction should succeed
  EXPECT_TRUE(destroy_result);

  // Assert: Root node should become invalid
  EXPECT_FALSE(parent.IsValid()); // Immediately invalidated

  // Assert: Descendents are lazily invalidated
  EXPECT_FALSE(child1.GetParent().has_value());
  EXPECT_FALSE(child1.IsValid());

  EXPECT_FALSE(child2.GetParent().has_value());
  EXPECT_FALSE(child2.IsValid());
}

} // namespace
