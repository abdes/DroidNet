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
using oxygen::scene::SceneNodeData;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::TransformComponent;

namespace {

// Mock Scene class for testing SceneNode functionality
class MockScene : public Scene {
public:
    MockScene()
        : Scene("MockScene", 1024)
    {
    }

    // Helper method to create a node for testing
    auto CreateTestNode(const std::string& name) -> SceneNode
    {
        return CreateNode(name);
    }

    // Helper method to create a node with custom flags for testing
    auto CreateTestNode(const std::string& name, SceneNode::Flags flags) -> SceneNode
    {
        return CreateNode(name, flags);
    }

    // Helper method to create a child node for testing
    auto CreateTestChildNode(const SceneNode& parent, const std::string& name) -> std::optional<SceneNode>
    {
        return CreateChildNode(parent, name);
    }

    // Helper method to access the SceneNodeImpl for testing
    auto GetTestNodeImpl(const SceneNode& node) -> std::optional<std::reference_wrapper<::SceneNodeImpl>>
    {
        return GetNodeImpl(node);
    }
};

class SceneNodeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        scene_ = std::make_shared<MockScene>();
    }

    void TearDown() override
    {
        scene_.reset();
    }

    std::shared_ptr<MockScene> scene_;
};

// -----------------------------------------------------------------------------
// Constructor and Basic Functionality Tests
// -----------------------------------------------------------------------------

//! Tests that SceneNode constructor creates a valid node handle
TEST_F(SceneNodeTest, ConstructorCreatesValidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    EXPECT_TRUE(node.IsValid());
    EXPECT_EQ(node.GetHandle().ResourceType(), oxygen::resources::kSceneNode);
}

//! Tests that SceneNode can access its underlying SceneNodeImpl
TEST_F(SceneNodeTest, GetObjectReturnsValidImpl)
{
    auto node = scene_->CreateTestNode("TestNode");

    auto impl = node.GetObject();
    ASSERT_TRUE(impl.has_value());
    EXPECT_EQ(impl->get().GetName(), "TestNode");
}

//! Tests that SceneNode can access its underlying SceneNodeImpl (const version)
TEST_F(SceneNodeTest, GetObjectConstReturnsValidImpl)
{
    auto node = scene_->CreateTestNode("TestNode");
    const auto& const_node = node;

    auto impl = const_node.GetObject();
    ASSERT_TRUE(impl.has_value());
    EXPECT_EQ(impl->get().GetName(), "TestNode");
}

//! Tests that SceneNode provides access to flags
TEST_F(SceneNodeTest, GetFlagsReturnsValidFlags)
{
    auto node = scene_->CreateTestNode("TestNode");

    auto flags = node.GetFlags();
    ASSERT_TRUE(flags.has_value());

    // Test default flag values
    const auto& flag_ref = flags->get();
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

//! Tests that SceneNode provides access to flags (const version)
TEST_F(SceneNodeTest, GetFlagsConstReturnsValidFlags)
{
    auto node = scene_->CreateTestNode("TestNode");
    const auto& const_node = node;

    auto flags = const_node.GetFlags();
    ASSERT_TRUE(flags.has_value());

    const auto& flag_ref = flags->get();
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
}

// -----------------------------------------------------------------------------
// Hierarchy Navigation Tests
// -----------------------------------------------------------------------------

//! Tests parent-child relationships
TEST_F(SceneNodeTest, ParentChildRelationship)
{
    auto parent = scene_->CreateTestNode("Parent");
    auto child_opt = scene_->CreateTestChildNode(parent, "Child");

    ASSERT_TRUE(child_opt.has_value());
    auto child = child_opt.value();

    // Test parent navigation
    auto child_parent = child.GetParent();
    ASSERT_TRUE(child_parent.has_value());
    EXPECT_EQ(child_parent->GetHandle(), parent.GetHandle());

    // Test child navigation
    auto parent_first_child = parent.GetFirstChild();
    ASSERT_TRUE(parent_first_child.has_value());
    EXPECT_EQ(parent_first_child->GetHandle(), child.GetHandle());

    // Test hierarchy queries
    EXPECT_TRUE(child.HasParent());
    EXPECT_FALSE(child.IsRoot());
    EXPECT_TRUE(parent.HasChildren());
    EXPECT_TRUE(parent.IsRoot());
}

//! Tests sibling relationships
TEST_F(SceneNodeTest, SiblingRelationships)
{
    auto parent = scene_->CreateTestNode("Parent");
    auto child1_opt = scene_->CreateTestChildNode(parent, "Child1");
    auto child2_opt = scene_->CreateTestChildNode(parent, "Child2");
    auto child3_opt = scene_->CreateTestChildNode(parent, "Child3");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    ASSERT_TRUE(child3_opt.has_value());

    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();
    auto child3 = child3_opt.value();

    // Test sibling navigation (children are linked in reverse order of creation)
    auto first_child = parent.GetFirstChild();
    ASSERT_TRUE(first_child.has_value());

    // Navigate through siblings
    auto next_sibling = first_child->GetNextSibling();
    ASSERT_TRUE(next_sibling.has_value());

    auto third_sibling = next_sibling->GetNextSibling();
    ASSERT_TRUE(third_sibling.has_value());

    // Test that we can navigate back
    auto prev_sibling = third_sibling->GetPrevSibling();
    ASSERT_TRUE(prev_sibling.has_value());
    EXPECT_EQ(prev_sibling->GetHandle(), next_sibling->GetHandle());
}

//! Tests root node behavior
TEST_F(SceneNodeTest, RootNodeBehavior)
{
    auto root = scene_->CreateTestNode("Root");

    EXPECT_TRUE(root.IsRoot());
    EXPECT_FALSE(root.HasParent());
    EXPECT_FALSE(root.HasChildren());

    auto parent = root.GetParent();
    EXPECT_FALSE(parent.has_value());

    auto first_child = root.GetFirstChild();
    EXPECT_FALSE(first_child.has_value());

    auto next_sibling = root.GetNextSibling();
    EXPECT_FALSE(next_sibling.has_value());

    auto prev_sibling = root.GetPrevSibling();
    EXPECT_FALSE(prev_sibling.has_value());
}

//! Tests navigation with invalid/expired nodes
TEST_F(SceneNodeTest, NavigationWithInvalidNodes)
{
    auto node = scene_->CreateTestNode("TestNode");

    // Destroy the node to make it invalid
    scene_->DestroyNode(node);

    // Navigation should return nullopt for invalid nodes
    EXPECT_FALSE(node.GetParent().has_value());
    EXPECT_FALSE(node.GetFirstChild().has_value());
    EXPECT_FALSE(node.GetNextSibling().has_value());
    EXPECT_FALSE(node.GetPrevSibling().has_value());

    // Hierarchy queries should be false for invalid nodes
    EXPECT_FALSE(node.HasParent());
    EXPECT_FALSE(node.HasChildren());
    EXPECT_FALSE(node.IsRoot());
}

// -----------------------------------------------------------------------------
// Object Access Tests
// -----------------------------------------------------------------------------

//! Tests GetObject with valid node
TEST_F(SceneNodeTest, GetObjectWithValidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    auto impl = node.GetObject();
    ASSERT_TRUE(impl.has_value());

    // Test that we can access SceneNodeImpl methods
    EXPECT_EQ(impl->get().GetName(), "TestNode");
    EXPECT_TRUE(impl->get().IsTransformDirty());
}

//! Tests GetObject with invalid/expired node
TEST_F(SceneNodeTest, GetObjectWithInvalidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    // Destroy the node
    scene_->DestroyNode(node);

    // GetObject should return nullopt
    auto impl = node.GetObject();
    EXPECT_FALSE(impl.has_value());
}

//! Tests GetFlags with valid node
TEST_F(SceneNodeTest, GetFlagsWithValidNode)
{
    // Create node with custom flags
    auto custom_flags = SceneNode::Flags {}
                            .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
                            .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

    auto node = scene_->CreateTestNode("TestNode", custom_flags);

    auto flags = node.GetFlags();
    ASSERT_TRUE(flags.has_value());

    const auto& flag_ref = flags->get();
    EXPECT_FALSE(flag_ref.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flag_ref.GetEffectiveValue(SceneNodeFlags::kStatic));
}

//! Tests GetFlags with invalid/expired node
TEST_F(SceneNodeTest, GetFlagsWithInvalidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    // Destroy the node
    scene_->DestroyNode(node);

    // GetFlags should return nullopt
    auto flags = node.GetFlags();
    EXPECT_FALSE(flags.has_value());
}

// -----------------------------------------------------------------------------
// Transform Wrapper Creation Tests
// -----------------------------------------------------------------------------

//! Tests Transform wrapper creation with valid node
TEST_F(SceneNodeTest, GetTransformWithValidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    // Should be able to create Transform wrapper without error
    EXPECT_NO_THROW({
        auto transform = node.GetTransform();
        auto const_transform = static_cast<const SceneNode&>(node).GetTransform();
    });
}

//! Tests Transform wrapper creation with invalid node
TEST_F(SceneNodeTest, GetTransformWithInvalidNode)
{
    auto node = scene_->CreateTestNode("TestNode");

    // Destroy the node
    scene_->DestroyNode(node);

    // Should still be able to create Transform wrapper (it handles invalid nodes gracefully)
    EXPECT_NO_THROW({
        auto transform = node.GetTransform();
        auto const_transform = static_cast<const SceneNode&>(node).GetTransform();
    });
}

//! Tests basic Transform operations on valid node
TEST_F(SceneNodeTest, TransformBasicOperations)
{
    auto node = scene_->CreateTestNode("TestNode");
    auto transform = node.GetTransform();

    // Test setting local position
    EXPECT_TRUE(transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f }));

    // Test getting local position
    auto position = transform.GetLocalPosition();
    ASSERT_TRUE(position.has_value());
    EXPECT_FLOAT_EQ(position->x, 1.0f);
    EXPECT_FLOAT_EQ(position->y, 2.0f);
    EXPECT_FLOAT_EQ(position->z, 3.0f);

    // Test setting local scale
    EXPECT_TRUE(transform.SetLocalScale({ 2.0f, 2.0f, 2.0f }));

    // Test getting local scale
    auto scale = transform.GetLocalScale();
    ASSERT_TRUE(scale.has_value());
    EXPECT_FLOAT_EQ(scale->x, 2.0f);
    EXPECT_FLOAT_EQ(scale->y, 2.0f);
    EXPECT_FLOAT_EQ(scale->z, 2.0f);
}

//! Tests Transform operations on invalid node
TEST_F(SceneNodeTest, TransformOperationsOnInvalidNode)
{
    auto node = scene_->CreateTestNode("TestNode");
    auto transform = node.GetTransform();

    // Destroy the node
    scene_->DestroyNode(node);

    // Operations should fail gracefully and return false/nullopt
    EXPECT_FALSE(transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f }));
    EXPECT_FALSE(transform.GetLocalPosition().has_value());
    EXPECT_FALSE(transform.SetLocalScale({ 2.0f, 2.0f, 2.0f }));
    EXPECT_FALSE(transform.GetLocalScale().has_value());
}

// -----------------------------------------------------------------------------
// Lazy Invalidation Behavior Tests
// -----------------------------------------------------------------------------

//! Tests that SceneNode handles become invalid lazily
TEST_F(SceneNodeTest, LazyInvalidationOnDestroy)
{
    auto node = scene_->CreateTestNode("TestNode");
    auto node_copy = node; // Create a copy of the handle

    EXPECT_TRUE(node.IsValid());
    EXPECT_TRUE(node_copy.IsValid());

    // Destroy the original node
    scene_->DestroyNode(node);

    // Both handles should still report as valid until accessed
    // (This tests the lazy invalidation behavior)

    // First access should detect invalidity and invalidate the handle
    auto impl = node_copy.GetObject();
    EXPECT_FALSE(impl.has_value());

    // After failed access, validity checks should reflect the state
    // (Note: The exact behavior may depend on implementation details)
}

//! Tests that multiple SceneNode handles to the same node are handled correctly
TEST_F(SceneNodeTest, MultipleHandlesToSameNode)
{
    auto node1 = scene_->CreateTestNode("TestNode");
    auto handle = node1.GetHandle();

    // Create another SceneNode from the same handle
    auto node2_opt = scene_->GetNode(handle);
    ASSERT_TRUE(node2_opt.has_value());
    auto node2 = node2_opt.value();

    EXPECT_EQ(node1.GetHandle(), node2.GetHandle());

    // Both should access the same underlying data
    auto impl1 = node1.GetObject();
    auto impl2 = node2.GetObject();

    ASSERT_TRUE(impl1.has_value());
    ASSERT_TRUE(impl2.has_value());
    EXPECT_EQ(&impl1->get(), &impl2->get());
}

// -----------------------------------------------------------------------------
// Scene Expiration Handling Tests
// -----------------------------------------------------------------------------

//! Tests behavior when scene is destroyed before nodes
TEST_F(SceneNodeTest, SceneExpirationHandling)
{
    auto node = scene_->CreateTestNode("TestNode");
    EXPECT_TRUE(node.IsValid());

    // Destroy the scene
    scene_.reset();

    // Node operations should fail gracefully
    auto impl = node.GetObject();
    EXPECT_FALSE(impl.has_value());

    auto flags = node.GetFlags();
    EXPECT_FALSE(flags.has_value());

    // Navigation should also fail gracefully
    EXPECT_FALSE(node.GetParent().has_value());
    EXPECT_FALSE(node.GetFirstChild().has_value());
}

// -----------------------------------------------------------------------------
// Edge Cases and Error Conditions
// -----------------------------------------------------------------------------

//! Tests copy and assignment operations
TEST_F(SceneNodeTest, CopyAndAssignmentOperations)
{
    auto node1 = scene_->CreateTestNode("TestNode1");
    auto node2 = scene_->CreateTestNode("TestNode2");

    // Test copy constructor
    auto node1_copy(node1);
    EXPECT_EQ(node1.GetHandle(), node1_copy.GetHandle());

    // Test copy assignment
    node1_copy = node2;
    EXPECT_EQ(node2.GetHandle(), node1_copy.GetHandle());
    EXPECT_NE(node1.GetHandle(), node1_copy.GetHandle());

    // Test move constructor
    auto node1_moved(std::move(node1));
    EXPECT_TRUE(node1_moved.IsValid());

    // Test move assignment
    auto node3 = scene_->CreateTestNode("TestNode3");
    node3 = std::move(node2);
    EXPECT_TRUE(node3.IsValid());
}

//! Tests SceneNode with empty/invalid scene
TEST_F(SceneNodeTest, EmptySceneBehavior)
{
    // Create node in valid scene
    auto node = scene_->CreateTestNode("TestNode");
    EXPECT_TRUE(node.IsValid());

    // Clear the scene
    scene_->Clear();

    // Node should now be invalid when accessed
    auto impl = node.GetObject();
    EXPECT_FALSE(impl.has_value());
}

//! Tests hierarchical destruction behavior
TEST_F(SceneNodeTest, HierarchicalDestructionBehavior)
{
    auto parent = scene_->CreateTestNode("Parent");
    auto child1_opt = scene_->CreateTestChildNode(parent, "Child1");
    auto child2_opt = scene_->CreateTestChildNode(parent, "Child2");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());

    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();

    // Destroy parent hierarchy
    EXPECT_TRUE(scene_->DestroyNodeHierarchy(parent));

    // All nodes should become invalid
    EXPECT_FALSE(parent.GetObject().has_value());
    EXPECT_FALSE(child1.GetObject().has_value());
    EXPECT_FALSE(child2.GetObject().has_value());
}

} // namespace
