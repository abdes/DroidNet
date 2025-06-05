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

namespace {

//=============================================================================
// Scene Basic Functionality Tests
//=============================================================================

class SceneBasicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        scene_ = std::make_shared<Scene>("TestScene", 1024);
    }
    void TearDown() override
    {
        scene_.reset();
    }
    SceneNode CreateNode(const std::string& name)
    {
        return scene_->CreateNode(name);
    }
    SceneNode CreateNode(const std::string& name, const SceneNode::Flags& flags)
    {
        return scene_->CreateNode(name, flags);
    }
    std::optional<SceneNode> CreateChildNode(const SceneNode& parent, const std::string& name)
    {
        return scene_->CreateChildNode(parent, name);
    }
    bool DestroyNode(SceneNode& node)
    {
        return scene_->DestroyNode(node);
    }
    bool DestroyNodeHierarchy(SceneNode& node)
    {
        return scene_->DestroyNodeHierarchy(node);
    }
    void ClearScene()
    {
        scene_->Clear();
    }
    void ExpectNodeValidWithName(const SceneNode& node, const std::string& name)
    {
        if (!node.IsValid())
            FAIL() << "Node should be valid";
        auto obj_opt = node.GetObject();
        if (!obj_opt.has_value())
            FAIL() << "Node object should be present";
        if (obj_opt->get().GetName() != name)
            FAIL() << "Node name mismatch: expected '" << name
                   << "', got '" << obj_opt->get().GetName() << "'";
    }
    void ExpectNodeLazyInvalidated(SceneNode& node)
    {
        // Node may appear valid, but after GetObject() it should be invalidated
        if (node.IsValid()) {
            auto obj_opt = node.GetObject();
            if (obj_opt.has_value())
                FAIL() << "Node should not have a valid object after destruction/clear";
            if (node.IsValid())
                FAIL() << "Node should be invalidated after failed access (lazy invalidation)";
        }
    }
    void ExpectNodeNotContainedAndInvalidated(SceneNode& node)
    {
        if (scene_->Contains(node))
            FAIL() << "Node should not be contained in scene";
        ExpectNodeLazyInvalidated(node);
    }
    void ExpectHandlesUnique(const SceneNode& n1, const SceneNode& n2, const SceneNode& n3)
    {
        if (n1.GetHandle() == n2.GetHandle()
            || n2.GetHandle() == n3.GetHandle()
            || n1.GetHandle() == n3.GetHandle())
            FAIL() << "Node handles should be unique";
    }
    void ExpectSceneEmpty()
    {
        if (!scene_->IsEmpty())
            FAIL() << "Scene should be empty";
        if (scene_->GetNodeCount() != 0)
            FAIL() << "Scene node count should be zero";
    }
    std::shared_ptr<Scene> scene_;
};

// Error/Assertion fixture
class SceneBasicErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        scene_ = std::make_shared<Scene>("TestScene", 1024);
    }
    void TearDown() override
    {
        scene_.reset();
    }
    SceneNode CreateNode(const std::string& name)
    {
        return scene_->CreateNode(name);
    }
    std::shared_ptr<Scene> scene_;
};

// -----------------------------------------------------------------------------
// Scene Construction and Metadata Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, SceneConstruction)
{
    // Arrange: No specific arrangement beyond fixture setup.

    // Act: Create three separate Scene instances with different names.
    auto scene1 = std::make_shared<Scene>("Scene1", 1024);
    auto scene2 = std::make_shared<Scene>("EmptyName", 1024);
    auto scene3 = std::make_shared<Scene>("Scene With Spaces", 1024);
    // Assert: Verify names are set correctly and new scenes are empty and have zero nodes.
    EXPECT_EQ(scene1->GetName(), "Scene1");
    EXPECT_EQ(scene2->GetName(), "EmptyName");
    EXPECT_EQ(scene3->GetName(), "Scene With Spaces");
    EXPECT_TRUE(scene1->IsEmpty());
    EXPECT_EQ(scene1->GetNodeCount(), 0);
}

NOLINT_TEST_F(SceneBasicTest, SceneNameOperations)
{
    // Arrange: scene_ is set up by the fixture with an initial name "TestScene".
    EXPECT_EQ(scene_->GetName(), "TestScene"); // Verify initial name.

    // Act: Set a new name for the scene.
    scene_->SetName("NewSceneName");
    // Assert: Verify the scene's name is updated to "NewSceneName".
    EXPECT_EQ(scene_->GetName(), "NewSceneName");

    // Act: Set an empty string as the scene name.
    scene_->SetName("");
    // Assert: Verify the scene's name is updated to an empty string.
    EXPECT_EQ(scene_->GetName(), "");

    // Act: Set a name containing special characters.
    scene_->SetName("Scene@#$%^&*()");
    // Assert: Verify the scene's name is updated to include special characters.
    EXPECT_EQ(scene_->GetName(), "Scene@#$%^&*()");
}

// -----------------------------------------------------------------------------
// Node Creation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, BasicNodeCreation)
{
    // Arrange: Scene is ready for use (fixture setup).
    // (scene_ is already set up)
    // Act: Create a single node with a specific name.
    auto node = CreateNode("TestNode");

    // Assert: Verify the node is valid, has the correct name, and scene
    // statistics are updated.
    ExpectNodeValidWithName(node, "TestNode");
    EXPECT_EQ(scene_->GetNodeCount(), 1);
}

NOLINT_TEST_F(SceneBasicTest, NodeCreationWithEmptyName)
{
    // Arrange: Scene is ready for use (fixture setup).
    // Act: Create a node with an empty name.
    auto node = CreateNode("");
    // Assert: Node should be valid and have an empty name.
    ExpectNodeValidWithName(node, "");
}

NOLINT_TEST_F(SceneBasicTest, NodeCreationWithCustomFlags)
{
    // Arrange: Define custom node flags (e.g., not visible, static).
    auto custom_flags
        = SceneNode::Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
              .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

    // Act: Create a node with the specified custom flags.
    auto node = CreateNode("FlaggedNode", custom_flags);

    // Assert: Verify the node is valid and its flags match the custom flags
    // set.
    EXPECT_TRUE(node.IsValid());
    auto flags_opt = node.GetFlags();
    ASSERT_TRUE(flags_opt.has_value());
    const auto& flags = flags_opt->get();
    EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flags.GetEffectiveValue(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneBasicTest, MultipleNodeCreation)
{
    // Arrange: Scene is ready for use (fixture setup).

    // Act: Create three distinct nodes.
    auto node1 = CreateNode("Node1");
    auto node2 = CreateNode("Node2");
    auto node3 = CreateNode("Node3");

    // Assert: All nodes should be valid, their handles unique, and scene count
    // updated correctly.
    EXPECT_TRUE(node1.IsValid());
    EXPECT_TRUE(node2.IsValid());
    EXPECT_TRUE(node3.IsValid());
    EXPECT_EQ(scene_->GetNodeCount(), 3);
    ExpectHandlesUnique(node1, node2, node3);
}

NOLINT_TEST_F(SceneBasicTest, ChildNodeCreation)
{
    // Arrange: Create a parent node and verify its validity.
    auto parent = CreateNode("Parent");
    EXPECT_TRUE(parent.IsValid());

    // Act: Create a child node for the previously created parent.
    auto child_opt = CreateChildNode(parent, "Child");

    // Assert: Verify the child was created, both parent and child are valid
    // with correct names, and scene node count is updated.
    ASSERT_TRUE(child_opt.has_value());
    auto child = child_opt.value();
    ExpectNodeValidWithName(parent, "Parent");
    ExpectNodeValidWithName(child, "Child");
    EXPECT_EQ(scene_->GetNodeCount(), 2);
}

// -----------------------------------------------------------------------------
// Node Destruction Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, BasicNodeDestruction)
{
    // Arrange: Create a single node and verify its initial valid state and
    // scene count.
    auto node = CreateNode("NodeToDestroy");
    EXPECT_TRUE(node.IsValid());
    EXPECT_EQ(scene_->GetNodeCount(), 1);

    // Act: Destroy the created node.
    bool destroyed = DestroyNode(node);

    // Assert: Verify successful destruction, node invalidation, and scene
    // emptiness.
    EXPECT_TRUE(destroyed);
    ExpectNodeLazyInvalidated(node);
    ExpectSceneEmpty();
}

NOLINT_TEST_F(SceneBasicTest, HierarchicalNodeDestruction)
{
    // Arrange: Create a parent node and two child nodes. Verify initial scene
    // count and child creation success.
    auto parent = CreateNode("Parent");
    auto child1_opt = CreateChildNode(parent, "Child1");
    auto child2_opt = CreateChildNode(parent, "Child2");
    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();
    EXPECT_EQ(scene_->GetNodeCount(), 3);

    // Act: Destroy the parent node and its entire hierarchy.
    bool destroyed = DestroyNodeHierarchy(parent);

    // Assert: Verify successful destruction, scene emptiness, and invalidation
    // of parent and all children.
    EXPECT_TRUE(destroyed);
    ExpectSceneEmpty();
    ExpectNodeNotContainedAndInvalidated(parent);
    ExpectNodeNotContainedAndInvalidated(child1);
    ExpectNodeNotContainedAndInvalidated(child2);
}

// -----------------------------------------------------------------------------
// Error/Assertion/Death Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicErrorTest, DestroyNonExistentNodeDeath)
{
    // Arrange: Create a node.
    auto node = CreateNode("Node");

    // Act: Destroy the node, making it non-existent for a subsequent operation.
    scene_->DestroyNode(node);

    // Assert: Expect death when attempting to destroy the (now non-existent)
    // node again.
    EXPECT_DEATH(scene_->DestroyNode(node), "expecting a valid node handle");
}

// -----------------------------------------------------------------------------
// Scene Basic Death Tests (CHECK_F assertions)
// -----------------------------------------------------------------------------

// New Fixture for Death Tests related to Scene basic operations CHECK_F assertions
class SceneBasicDeathTest : public ::testing::Test {
protected:
    std::shared_ptr<Scene> scene_;
    // SceneNode is already in scope via: using oxygen::scene::SceneNode;

    void SetUp() override
    {
        // Initialize scene with a small capacity, sufficient for these tests
        scene_ = std::make_shared<Scene>("TestDeathScene", 100);
    }

    void TearDown() override
    {
        scene_.reset();
    }

    // Helper to create an invalidated node for testing
    // Creates a node and then destroys it, returning the now-invalidated SceneNode object.
    SceneNode CreateInvalidatedNode(const std::string& name = "InvalidNode")
    {
        auto node = scene_->CreateNode(name);
        // Assuming CreateNode aborts on failure (e.g. scene full) as per its documentation,
        // 'node' here should be valid if we reach this point.
        // DestroyNode will call node.Invalidate() on the 'node' object.
        scene_->DestroyNode(node);
        return node;
    }
};

NOLINT_TEST_F(SceneBasicDeathTest, CreateChildNodeWithInvalidParentDeath)
{
    SceneNode invalidParent = CreateInvalidatedNode("InvalidParentForChild");
    // This should trigger: CHECK_F(parent.IsValid(), "expecting a valid parent
    // handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->CreateChildNode(invalidParent, "ChildOfInvalid"),
        ".*expecting a valid parent handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, DestroyNodeWithChildrenDeath)
{
    auto parent = scene_->CreateNode("ParentWithChild");
    ASSERT_TRUE(parent.IsValid()); // Ensure parent is valid
    auto child_opt = scene_->CreateChildNode(parent, "Child");
    ASSERT_TRUE(child_opt.has_value() && child_opt->IsValid()); // Ensure child is valid and created

    // This should trigger: CHECK_F(!node.HasChildren(), "node has children, use
    // DestroyNodeHierarchy() instead");
    ASSERT_DEATH(scene_->DestroyNode(parent),
        ".*node has children.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, DestroyNodeHierarchyWithInvalidRootDeath)
{
    SceneNode invalidRoot = CreateInvalidatedNode("InvalidRootForHierarchy");
    // This should trigger: CHECK_F(root.IsValid(), "expecting a valid root node
    // handle");
    ASSERT_DEATH(scene_->DestroyNodeHierarchy(invalidRoot),
        ".*expecting a valid root node handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, GetParentFromInvalidNodeDeath)
{
    SceneNode invalidNode = CreateInvalidatedNode("InvalidNodeForGetParent");
    // This should trigger: CHECK_F(node.IsValid(), "expecting a valid node handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->GetParent(invalidNode),
        ".*expecting a valid node handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, GetFirstChildFromInvalidNodeDeath)
{
    SceneNode invalidNode = CreateInvalidatedNode("InvalidNodeForGetFirstChild");
    // This should trigger: CHECK_F(node.IsValid(), "expecting a valid node handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->GetFirstChild(invalidNode),
        ".*expecting a valid node handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, GetNextSiblingFromInvalidNodeDeath)
{
    SceneNode invalidNode = CreateInvalidatedNode("InvalidNodeForGetNextSibling");
    // This should trigger: CHECK_F(node.IsValid(), "expecting a valid node handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->GetNextSibling(invalidNode),
        ".*expecting a valid node handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, GetPrevSiblingFromInvalidNodeDeath)
{
    SceneNode invalidNode = CreateInvalidatedNode("InvalidNodeForGetPrevSibling");
    // Assuming GetPrevSibling has a similar CHECK_F for node.IsValid()
    // This should trigger: CHECK_F(node.IsValid(), "expecting a valid node handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->GetPrevSibling(invalidNode),
        ".*expecting a valid node handle.*");
}

NOLINT_TEST_F(SceneBasicDeathTest, DestroyInvalidNodeDeath)
{
    SceneNode invalidNode = CreateInvalidatedNode("InvalidNodeForDestroy");
    // This should trigger: CHECK_F(node.IsValid(), "expecting a valid node handle");
    ASSERT_DEATH([[maybe_unused]] auto _ = scene_->DestroyNode(invalidNode),
        ".*expecting a valid node handle.*");
}

// -----------------------------------------------------------------------------
// Node Containment Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, ContainsSceneNode)
{
    // Arrange: Create a test node.
    auto node = CreateNode("TestNode");

    // Act & Assert: Verify containment before node destruction and
    // non-containment after.
    EXPECT_TRUE(scene_->Contains(node));
    DestroyNode(node);
    EXPECT_FALSE(scene_->Contains(node));
}

NOLINT_TEST_F(SceneBasicTest, ContainsNodeHandle)
{
    // Arrange: Create a test node and get its handle.
    auto node = CreateNode("TestNode");
    auto handle = node.GetHandle();

    // Act & Assert: Verify containment of the handle before node destruction
    // and non-containment after.
    EXPECT_TRUE(scene_->Contains(handle));
    DestroyNode(node);
    EXPECT_FALSE(scene_->Contains(handle));
}

NOLINT_TEST_F(SceneBasicTest, ContainsNodeFromDifferentScene)
{
    // Arrange: Create a node in a separate, different scene.
    auto other_scene = std::make_shared<Scene>("OtherScene", 1024);
    auto other_node = other_scene->CreateNode("OtherNode");

    // Assert: Verify the current scene does not contain the foreign
    // node/handle, while the other scene correctly reports containment.
    EXPECT_FALSE(scene_->Contains(other_node));
    EXPECT_FALSE(scene_->Contains(other_node.GetHandle()));
    EXPECT_TRUE(other_scene->Contains(other_node));
}

// -----------------------------------------------------------------------------
// Scene Statistics Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, NodeCountAccuracy)
{
    // Arrange & Assert: Verify scene is initially empty and node count is zero.
    EXPECT_EQ(scene_->GetNodeCount(), 0);
    EXPECT_TRUE(scene_->IsEmpty());

    // Act: Create node1.
    auto node1 = CreateNode("Node1");
    // Assert: Verify node count is 1 and scene is not empty.
    EXPECT_EQ(scene_->GetNodeCount(), 1);
    EXPECT_FALSE(scene_->IsEmpty());

    // Act: Create node2.
    auto node2 = CreateNode("Node2");
    // Assert: Verify node count is 2.
    EXPECT_EQ(scene_->GetNodeCount(), 2);

    // Act: Create node3.
    auto node3 = CreateNode("Node3");
    // Assert: Verify node count is 3.
    EXPECT_EQ(scene_->GetNodeCount(), 3);

    // Act: Destroy node2.
    DestroyNode(node2);
    // Assert: Verify node count is 2.

    EXPECT_EQ(scene_->GetNodeCount(), 2);
    // Act: Destroy node1.
    DestroyNode(node1);
    // Assert: Verify node count is 1.
    EXPECT_EQ(scene_->GetNodeCount(), 1);

    // Act: Destroy node3.
    DestroyNode(node3);
    // Assert: Verify node count is 0 and scene is empty.
    EXPECT_EQ(scene_->GetNodeCount(), 0);
    EXPECT_TRUE(scene_->IsEmpty());
}

NOLINT_TEST_F(SceneBasicTest, IsEmptyBehavior)
{
    // Arrange & Assert: Verify scene is initially empty.
    EXPECT_TRUE(scene_->IsEmpty());

    // Act: Create a node.
    auto node = CreateNode("Node");
    // Assert: Verify scene is no longer empty.
    EXPECT_FALSE(scene_->IsEmpty());

    // Act: Destroy the node.
    DestroyNode(node);
    // Assert: Verify scene is empty again.
    EXPECT_TRUE(scene_->IsEmpty());
}

// -----------------------------------------------------------------------------
// Scene Clearing Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, SceneClear)
{
    // Arrange: Create a hierarchy (parent, two children) and a standalone node.
    // Verify initial node count and non-empty state.
    auto parent = CreateNode("Parent");
    auto child1_opt = CreateChildNode(parent, "Child1");
    auto child2_opt = CreateChildNode(parent, "Child2");
    auto standalone = CreateNode("Standalone");
    EXPECT_EQ(scene_->GetNodeCount(), 4);
    EXPECT_FALSE(scene_->IsEmpty());

    // Act: Clear the entire scene.
    ClearScene();

    // Assert: Verify scene is empty, node count is zero, and all previously
    // created nodes are invalidated and not contained.
    EXPECT_EQ(scene_->GetNodeCount(), 0);
    ExpectSceneEmpty();
    ExpectNodeNotContainedAndInvalidated(parent);
    if (child1_opt.has_value()) {
        ExpectNodeNotContainedAndInvalidated(child1_opt.value());
    }
    if (child2_opt.has_value()) {
        ExpectNodeNotContainedAndInvalidated(child2_opt.value());
    }
    ExpectNodeNotContainedAndInvalidated(standalone);
}

// -----------------------------------------------------------------------------
// Scene Defragmentation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, DefragmentStorage)
{
    // Arrange: Create three nodes, destroy the middle one to induce
    // fragmentation, and verify node count.
    auto node1 = CreateNode("Node1");
    auto node2 = CreateNode("Node2");
    auto node3 = CreateNode("Node3");
    DestroyNode(node2);
    EXPECT_EQ(scene_->GetNodeCount(), 2);

    // Act: Defragment the scene's storage.
    scene_->DefragmentStorage();

    // Assert: Verify node count is maintained, remaining nodes are still valid,
    // and the destroyed node remains invalid.
    EXPECT_EQ(scene_->GetNodeCount(), 2);
    EXPECT_TRUE(node1.IsValid());
    EXPECT_FALSE(node2.IsValid());
    EXPECT_TRUE(node3.IsValid());
}

// -----------------------------------------------------------------------------
// Edge Cases and Error Handling Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, SpecialCharacterNames)
{
    // Arrange: Scene is ready for node creation (fixture setup).

    // Act: Create nodes with names containing various special characters (e.g.,
    // symbols, spaces, control characters).
    auto node1 = CreateNode("Node@#$%");
    auto node2 = CreateNode("Node With Spaces");
    auto node3 = CreateNode("Node\tWith\nSpecial\rChars");
    auto node4 = CreateNode("Node_with-symbols.123");

    // Assert: Verify all nodes are valid and their names are correctly stored
    // and retrieved, preserving special characters.
    EXPECT_TRUE(node1.IsValid());
    EXPECT_TRUE(node2.IsValid());
    EXPECT_TRUE(node3.IsValid());
    EXPECT_TRUE(node4.IsValid());
    auto obj1 = node1.GetObject();
    auto obj2 = node2.GetObject();
    auto obj3 = node3.GetObject();
    auto obj4 = node4.GetObject();
    ASSERT_TRUE(obj1.has_value());
    ASSERT_TRUE(obj2.has_value());
    ASSERT_TRUE(obj3.has_value());
    ASSERT_TRUE(obj4.has_value());
    EXPECT_EQ(obj1->get().GetName(), "Node@#$%");
    EXPECT_EQ(obj2->get().GetName(), "Node With Spaces");
    EXPECT_EQ(obj3->get().GetName(), "Node\tWith\nSpecial\rChars");
    EXPECT_EQ(obj4->get().GetName(), "Node_with-symbols.123");
}

NOLINT_TEST_F(SceneBasicTest, VeryLongNodeNames)
{
    // Arrange: Prepare a very long string to be used as a node name.
    std::string long_name(1000, 'A');

    // Act: Create a node using the prepared very long name.
    auto node = CreateNode(long_name);

    // Assert: Verify the node is valid and its name is correctly stored and
    // retrieved, matching the long string.
    EXPECT_TRUE(node.IsValid());
    auto obj = node.GetObject();
    ASSERT_TRUE(obj.has_value());
    EXPECT_EQ(obj->get().GetName(), long_name);
}

NOLINT_TEST_F(SceneBasicTest, UnicodeCharacterNames)
{
    // Arrange: Scene is ready for node creation (fixture setup).

    // Act: Create nodes with names containing various Unicode characters (e.g.,
    // Japanese, Cyrillic, Emojis).
    auto node1 = CreateNode("Node_こんにちは");
    auto node2 = CreateNode("Node_Здравствуй");
    auto node3 = CreateNode("Node_🚀🌟");

    // Assert: Verify all nodes are valid and their names are correctly stored
    // and retrieved, preserving Unicode characters.
    EXPECT_TRUE(node1.IsValid());
    EXPECT_TRUE(node2.IsValid());
    EXPECT_TRUE(node3.IsValid());
    auto obj1 = node1.GetObject();
    auto obj2 = node2.GetObject();
    auto obj3 = node3.GetObject();
    ASSERT_TRUE(obj1.has_value());
    ASSERT_TRUE(obj2.has_value());
    ASSERT_TRUE(obj3.has_value());
    EXPECT_EQ(obj1->get().GetName(), "Node_こんにちは");
    EXPECT_EQ(obj2->get().GetName(), "Node_Здравствуй");
    EXPECT_EQ(obj3->get().GetName(), "Node_🚀🌟");
}

} // namespace
