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
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::ObjectMetaData;
using oxygen::ResourceHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneNodeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Create a scene for all tests
        scene_ = std::make_shared<Scene>("TestScene", 1024);
    }

    void TearDown() override
    {
        // Clean up: Reset scene pointer to ensure proper cleanup
        scene_.reset();
    }

    // Helper: Verify node is valid and has expected name
    void ExpectNodeValidWithName(const SceneNode& node, const std::string& expected_name)
    {
        EXPECT_TRUE(node.IsValid());
        auto impl = node.GetObject();
        ASSERT_TRUE(impl.has_value());
        EXPECT_EQ(impl->get().GetName(), expected_name);
    }

    // Helper: Set transform values for testing
    void SetTransformValues(const SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
    {
        auto transform = node.GetTransform();
        EXPECT_TRUE(transform.SetLocalPosition(position));
        EXPECT_TRUE(transform.SetLocalScale(scale));
    }

    // Helper: Verify transform values match expected
    void ExpectTransformValues(const SceneNode& node, const glm::vec3& expected_pos, const glm::vec3& expected_scale)
    {
        auto transform = node.GetTransform();
        auto position = transform.GetLocalPosition();
        auto scale = transform.GetLocalScale();

        ASSERT_TRUE(position.has_value());
        ASSERT_TRUE(scale.has_value());
        EXPECT_EQ(*position, expected_pos);
        EXPECT_EQ(*scale, expected_scale);
    }

    std::shared_ptr<Scene> scene_;
};

//------------------------------------------------------------------------------
// Constructor and Basic Functionality Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, Constructor_CreatesValidNodeHandle)
{
    // Arrange: Scene is ready (done in SetUp)

    // Act: Create a test node
    auto node = scene_->CreateNode("TestNode");

    // Assert: Node should be valid with correct resource type
    EXPECT_TRUE(node.IsValid());
    EXPECT_EQ(node.GetHandle().ResourceType(), oxygen::resources::kSceneNode);
}

NOLINT_TEST_F(SceneNodeTest, GetObject_ReturnsValidImplementation)
{
    // Arrange: Create a test node
    auto node = scene_->CreateNode("TestNode");

    // Act: Get the underlying implementation
    auto impl = node.GetObject();

    // Assert: Implementation should be accessible with correct name
    ASSERT_TRUE(impl.has_value());
    EXPECT_EQ(impl->get().GetName(), "TestNode");
}

NOLINT_TEST_F(SceneNodeTest, GetObjectConst_ReturnsValidImplementation)
{
    // Arrange: Create a test node and get const reference
    auto node = scene_->CreateNode("TestNode");
    const auto& const_node = node;

    // Act: Get implementation through const reference
    auto impl = const_node.GetObject();

    // Assert: Const access should work correctly
    ASSERT_TRUE(impl.has_value());
    EXPECT_EQ(impl->get().GetName(), "TestNode");
}

NOLINT_TEST_F(SceneNodeTest, GetFlags_ReturnsValidFlagsWithDefaults)
{
    // Arrange: Create a test node
    auto node = scene_->CreateNode("TestNode");

    // Act: Get node flags
    auto flags = node.GetFlags();

    // Assert: Flags should be accessible with expected default values
    ASSERT_TRUE(flags.has_value());
    const auto& flag_ref = flags->get();
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneNodeTest, GetFlagsConst_ReturnsValidFlags)
{
    // Arrange: Create a test node and get const reference
    auto node = scene_->CreateNode("TestNode");
    const auto& const_node = node;

    // Act: Get flags through const reference
    auto flags = const_node.GetFlags();

    // Assert: Const flags access should work correctly
    ASSERT_TRUE(flags.has_value());
    const auto& flag_ref = flags->get();
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Hierarchy Navigation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, ParentChildRelationship_NavigationWorks)
{
    // Arrange: Create parent and child nodes
    auto parent = scene_->CreateNode("Parent");
    auto child_opt = scene_->CreateChildNode(parent, "Child");
    ASSERT_TRUE(child_opt.has_value());
    auto child = child_opt.value();

    // Act & Assert: Test parent navigation from child
    auto child_parent = child.GetParent();
    ASSERT_TRUE(child_parent.has_value());
    EXPECT_EQ(child_parent->GetHandle(), parent.GetHandle());

    // Act & Assert: Test child navigation from parent
    auto parent_first_child = parent.GetFirstChild();
    ASSERT_TRUE(parent_first_child.has_value());
    EXPECT_EQ(parent_first_child->GetHandle(), child.GetHandle());

    // Act & Assert: Test hierarchy queries
    EXPECT_TRUE(child.HasParent());
    EXPECT_FALSE(child.IsRoot());
    EXPECT_TRUE(parent.HasChildren());
    EXPECT_TRUE(parent.IsRoot());
}

NOLINT_TEST_F(SceneNodeTest, SiblingRelationships_NavigationWorks)
{
    // Arrange: Create parent with multiple children
    auto parent = scene_->CreateNode("Parent");
    auto child1_opt = scene_->CreateChildNode(parent, "Child1");
    auto child2_opt = scene_->CreateChildNode(parent, "Child2");
    auto child3_opt = scene_->CreateChildNode(parent, "Child3");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    ASSERT_TRUE(child3_opt.has_value());

    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();
    auto child3 = child3_opt.value();

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

NOLINT_TEST_F(SceneNodeTest, RootNode_BehavesCorrectly)
{
    // Arrange: Create a root node
    auto root = scene_->CreateNode("Root");

    // Act & Assert: Root node should have expected properties
    EXPECT_TRUE(root.IsRoot());
    EXPECT_FALSE(root.HasParent());
    EXPECT_FALSE(root.HasChildren());

    // Act & Assert: Navigation should return empty optionals
    EXPECT_FALSE(root.GetParent().has_value());
    EXPECT_FALSE(root.GetFirstChild().has_value());
    EXPECT_FALSE(root.GetNextSibling().has_value());
    EXPECT_FALSE(root.GetPrevSibling().has_value());
}

NOLINT_TEST_F(SceneNodeTest, NavigationWithInvalidNodes_ReturnsEmpty)
{
    // Arrange: Create a node then destroy it
    auto node = scene_->CreateNode("TestNode");
    scene_->DestroyNode(node);

    // Act & Assert: Navigation should return empty optionals for invalid nodes
    EXPECT_FALSE(node.GetParent().has_value());
    EXPECT_FALSE(node.GetFirstChild().has_value());
    EXPECT_FALSE(node.GetNextSibling().has_value());
    EXPECT_FALSE(node.GetPrevSibling().has_value());

    // Act & Assert: Hierarchy queries should be false for invalid nodes
    EXPECT_FALSE(node.HasParent());
    EXPECT_FALSE(node.HasChildren());
    EXPECT_FALSE(node.IsRoot());
}

//------------------------------------------------------------------------------
// Object Access Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, GetObjectWithValidNode_AccessesImplementation)
{
    // Arrange: Create a valid test node
    auto node = scene_->CreateNode("TestNode");

    // Act: Get the implementation object
    auto impl = node.GetObject();

    // Assert: Should access SceneNodeImpl methods correctly
    ASSERT_TRUE(impl.has_value());
    EXPECT_EQ(impl->get().GetName(), "TestNode");
    EXPECT_TRUE(impl->get().IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeTest, GetObjectWithInvalidNode_ReturnsEmpty)
{
    // Arrange: Create a node then destroy it to make it invalid
    auto node = scene_->CreateNode("TestNode");
    scene_->DestroyNode(node);

    // Act: Attempt to get object from invalid node
    auto impl = node.GetObject();

    // Assert: Should return empty optional
    EXPECT_FALSE(impl.has_value());
}

NOLINT_TEST_F(SceneNodeTest, GetFlagsWithValidNode_AccessesCustomFlags)
{
    // Arrange: Create node with custom flags
    auto custom_flags
        = SceneNode::Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
              .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));
    auto node = scene_->CreateNode("TestNode", custom_flags);

    // Act: Get the flags
    auto flags = node.GetFlags();

    // Assert: Custom flags should be preserved
    ASSERT_TRUE(flags.has_value());
    const auto& flag_ref = flags->get();
    EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneNodeTest, GetFlagsWithInvalidNode_ReturnsEmpty)
{
    // Arrange: Create a node then destroy it
    auto node = scene_->CreateNode("TestNode");
    scene_->DestroyNode(node);

    // Act: Attempt to get flags from invalid node
    auto flags = node.GetFlags();

    // Assert: Should return empty optional
    EXPECT_FALSE(flags.has_value());
}

//------------------------------------------------------------------------------
// Transform Wrapper Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, GetTransformWithValidNode_CreatesWrapper)
{
    // Arrange: Create a valid test node
    auto node = scene_->CreateNode("TestNode");

    // Act & Assert: Should be able to create Transform wrapper without error
    EXPECT_NO_THROW({
        auto transform = node.GetTransform();
        auto const_transform = static_cast<const SceneNode&>(node).GetTransform();
    });
}

NOLINT_TEST_F(SceneNodeTest, GetTransformWithInvalidNode_HandlesGracefully)
{
    // Arrange: Create a node then destroy it
    auto node = scene_->CreateNode("TestNode");
    scene_->DestroyNode(node);

    // Act & Assert: Should still create wrapper (handles invalid nodes gracefully)
    EXPECT_NO_THROW({
        auto transform = node.GetTransform();
        auto const_transform = static_cast<const SceneNode&>(node).GetTransform();
    });
}

NOLINT_TEST_F(SceneNodeTest, TransformBasicOperations_WorkOnValidNode)
{
    // Arrange: Create a valid node and get transform wrapper
    auto node = scene_->CreateNode("TestNode");
    auto transform = node.GetTransform();

    // Act: Set local position
    auto set_position_result = transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f });

    // Assert: Position should be set successfully
    EXPECT_TRUE(set_position_result);

    // Act: Get local position
    auto position = transform.GetLocalPosition();

    // Assert: Position should match what was set
    ASSERT_TRUE(position.has_value());
    EXPECT_FLOAT_EQ(position->x, 1.0f);
    EXPECT_FLOAT_EQ(position->y, 2.0f);
    EXPECT_FLOAT_EQ(position->z, 3.0f);

    // Act: Set local scale
    auto set_scale_result = transform.SetLocalScale({ 2.0f, 2.0f, 2.0f });

    // Assert: Scale should be set successfully
    EXPECT_TRUE(set_scale_result);

    // Act: Get local scale
    auto scale = transform.GetLocalScale();

    // Assert: Scale should match what was set
    ASSERT_TRUE(scale.has_value());
    EXPECT_FLOAT_EQ(scale->x, 2.0f);
    EXPECT_FLOAT_EQ(scale->y, 2.0f);
    EXPECT_FLOAT_EQ(scale->z, 2.0f);
}

NOLINT_TEST_F(SceneNodeTest, TransformOperationsOnInvalidNode_FailGracefully)
{
    // Arrange: Create node, get transform, then destroy node
    auto node = scene_->CreateNode("TestNode");
    auto transform = node.GetTransform();
    scene_->DestroyNode(node);

    // Act & Assert: Operations should fail gracefully and return false/nullopt
    EXPECT_FALSE(transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f }));
    EXPECT_FALSE(transform.GetLocalPosition().has_value());
    EXPECT_FALSE(transform.SetLocalScale({ 2.0f, 2.0f, 2.0f }));
    EXPECT_FALSE(transform.GetLocalScale().has_value());
}

//------------------------------------------------------------------------------
// Lazy Invalidation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, LazyInvalidation_HandlesDestroyedNodes)
{
    // Arrange: Create node and copy handle
    auto node = scene_->CreateNode("TestNode");
    auto node_copy = node;

    EXPECT_TRUE(node.IsValid());
    EXPECT_TRUE(node_copy.IsValid());

    // Act: Destroy the original node
    scene_->DestroyNode(node);

    // Act: First access should detect invalidity
    auto impl = node_copy.GetObject();

    // Assert: Access should fail and return empty optional
    EXPECT_FALSE(impl.has_value());
}

NOLINT_TEST_F(SceneNodeTest, MultipleHandlesToSameNode_ShareUnderlyingData)
{
    // Arrange: Create node and get second handle to same node
    auto node1 = scene_->CreateNode("TestNode");
    auto handle = node1.GetHandle();
    auto node2_opt = scene_->GetNode(handle);

    ASSERT_TRUE(node2_opt.has_value());
    auto node2 = node2_opt.value();

    // Act & Assert: Handles should be identical
    EXPECT_EQ(node1.GetHandle(), node2.GetHandle());

    // Act: Get implementations from both handles
    auto impl1 = node1.GetObject();
    auto impl2 = node2.GetObject();

    // Assert: Both should access the same underlying data
    ASSERT_TRUE(impl1.has_value());
    ASSERT_TRUE(impl2.has_value());
    EXPECT_EQ(&impl1->get(), &impl2->get());
}

//------------------------------------------------------------------------------
// Scene Expiration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, SceneExpiration_NodesFailGracefully)
{
    // Arrange: Create a node in valid scene
    auto node = scene_->CreateNode("TestNode");
    EXPECT_TRUE(node.IsValid());

    // Act: Destroy the scene
    scene_.reset();

    // Act & Assert: Node operations should fail gracefully
    auto impl = node.GetObject();
    EXPECT_FALSE(impl.has_value());

    auto flags = node.GetFlags();
    EXPECT_FALSE(flags.has_value());

    // Act & Assert: Navigation should also fail gracefully
    EXPECT_FALSE(node.GetParent().has_value());
    EXPECT_FALSE(node.GetFirstChild().has_value());
}

//------------------------------------------------------------------------------
// Copy and Move Semantics Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, CopyConstructor_PreservesHandle)
{
    // Arrange: Create a test node
    auto node1 = scene_->CreateNode("TestNode1");

    // Act: Copy construct new node
    auto node1_copy(node1);

    // Assert: Copy should have same handle
    EXPECT_EQ(node1.GetHandle(), node1_copy.GetHandle());
}

NOLINT_TEST_F(SceneNodeTest, CopyAssignment_UpdatesHandle)
{
    // Arrange: Create two different nodes
    auto node1 = scene_->CreateNode("TestNode1");
    auto node2 = scene_->CreateNode("TestNode2");

    // Act: Copy assign node2 to node1_copy
    auto node1_copy = node1;
    node1_copy = node2;

    // Assert: Assignment should update handle
    EXPECT_EQ(node2.GetHandle(), node1_copy.GetHandle());
    EXPECT_NE(node1.GetHandle(), node1_copy.GetHandle());
}

NOLINT_TEST_F(SceneNodeTest, MoveConstructor_TransfersHandle)
{
    // Arrange: Create a test node
    auto node1 = scene_->CreateNode("TestNode1");
    auto expected_handle = node1.GetHandle();

    // Act: Move construct new node
    auto node1_moved(std::move(node1));

    // Assert: Moved node should have the handle
    EXPECT_TRUE(node1_moved.IsValid());
    EXPECT_EQ(node1_moved.GetHandle(), expected_handle);
}

NOLINT_TEST_F(SceneNodeTest, MoveAssignment_TransfersHandle)
{
    // Arrange: Create two nodes
    auto node2 = scene_->CreateNode("TestNode2");
    auto node3 = scene_->CreateNode("TestNode3");
    auto expected_handle = node2.GetHandle();

    // Act: Move assign node2 to node3
    node3 = std::move(node2);

    // Assert: Move assignment should transfer handle
    EXPECT_TRUE(node3.IsValid());
    EXPECT_EQ(node3.GetHandle(), expected_handle);
}

//------------------------------------------------------------------------------
// Edge Cases and Error Conditions Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneNodeTest, EmptyScene_NodesFailGracefully)
{
    // Arrange: Create node in valid scene
    auto node = scene_->CreateNode("TestNode");
    EXPECT_TRUE(node.IsValid());

    // Act: Clear the scene
    scene_->Clear();

    // Act: Try to access node after scene clear
    auto impl = node.GetObject();

    // Assert: Node should now be invalid when accessed
    EXPECT_FALSE(impl.has_value());
}

NOLINT_TEST_F(SceneNodeTest, HierarchicalDestruction_InvalidatesAllNodes)
{
    // Arrange: Create parent-child hierarchy
    auto parent = scene_->CreateNode("Parent");
    auto child1_opt = scene_->CreateChildNode(parent, "Child1");
    auto child2_opt = scene_->CreateChildNode(parent, "Child2");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();

    // Act: Destroy parent hierarchy
    auto destroy_result = scene_->DestroyNodeHierarchy(parent);

    // Assert: Destruction should succeed
    EXPECT_TRUE(destroy_result);

    // Assert: All nodes should become invalid
    EXPECT_FALSE(parent.GetObject().has_value());
    EXPECT_FALSE(child1.GetObject().has_value());
    EXPECT_FALSE(child2.GetObject().has_value());
}

NOLINT_TEST_F(SceneNodeTest, TransformIntegration_ModificationsPreserved)
{
    // Arrange: Create node and set initial transform
    auto node = scene_->CreateNode("TestNode");
    const auto initial_pos = glm::vec3 { 1.0f, 2.0f, 3.0f };
    const auto initial_scale = glm::vec3 { 2.0f, 2.0f, 2.0f };

    SetTransformValues(node, initial_pos, initial_scale);

    // Act: Verify initial values are set
    ExpectTransformValues(node, initial_pos, initial_scale);

    // Act: Modify transform values
    const auto new_pos = glm::vec3 { 10.0f, 20.0f, 30.0f };
    const auto new_scale = glm::vec3 { 3.0f, 3.0f, 3.0f };
    SetTransformValues(node, new_pos, new_scale);

    // Assert: New values should be preserved
    ExpectTransformValues(node, new_pos, new_scale);
}

} // namespace
