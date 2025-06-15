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

namespace {

//=============================================================================
// Scene Basic Functionality Tests
//=============================================================================

class SceneBasicTest : public testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene", 1024); }
  void TearDown() override { scene_.reset(); }

  [[nodiscard]] auto CreateNode(const std::string& name) const -> SceneNode
  {
    return scene_->CreateNode(name);
  }

  [[nodiscard]] auto CreateNode(
    const std::string& name, const SceneNode::Flags& flags) const -> SceneNode
  {
    return scene_->CreateNode(name, flags);
  }

  [[nodiscard]] auto CreateChildNode(SceneNode& parent,
    const std::string& name) const -> std::optional<SceneNode>
  {
    return scene_->CreateChildNode(parent, name);
  }

  [[nodiscard]] auto CreateNodeWithInvalidScene() const -> SceneNode
  {
    return {};
  }

  [[nodiscard]] auto CreateNodeWithInvalidHandle() const -> SceneNode
  {
    return SceneNode(scene_);
  }

  auto DestroyNode(SceneNode& node) const -> bool
  {
    return scene_->DestroyNode(node);
  }

  auto DestroyNodeHierarchy(SceneNode& node) const -> bool
  {
    return scene_->DestroyNodeHierarchy(node);
  }

  void ClearScene() const { scene_->Clear(); }

  static void ExpectNodeValidWithName(
    const SceneNode& node, const std::string& name)
  {
    EXPECT_TRUE(node.IsValid());
    EXPECT_EQ(node.GetName(), name);
  }

  static void ExpectNodeLazyInvalidated(SceneNode& node)
  {
    // Node may appear valid, but after GetObject() it should be invalidated
    if (node.IsValid()) {
      if (const auto obj_opt = node.GetObject(); obj_opt.has_value())
        FAIL() << "Node should not have a valid object after "
                  "destruction/clear";
      if (node.IsValid())
        FAIL() << "Node should be invalidated after failed access "
                  "(lazy invalidation)";
    }
  }
  void ExpectNodeNotInScene(const SceneNode& node) const
  {
    if (scene_->Contains(node))
      FAIL() << "Node should not be contained in scene";
  }

  static void ExpectHandlesUnique(
    const SceneNode& n1, const SceneNode& n2, const SceneNode& n3)
  {
    if (n1.GetHandle() == n2.GetHandle() || n2.GetHandle() == n3.GetHandle()
      || n1.GetHandle() == n3.GetHandle())
      FAIL() << "Node handles should be unique";
  }
  void ExpectSceneEmpty() const
  {
    if (!scene_->IsEmpty())
      FAIL() << "Scene should be empty";
    if (scene_->GetNodeCount() != 0)
      FAIL() << "Scene node count should be zero";
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
  const auto scene1 = std::make_shared<Scene>("Scene1", 1024);
  const auto scene2 = std::make_shared<Scene>("EmptyName", 1024);
  const auto scene3 = std::make_shared<Scene>("Scene With Spaces", 1024);
  // Assert: Verify names are set correctly and new scenes are empty and have
  // zero nodes.
  EXPECT_EQ(scene1->GetName(), "Scene1");
  EXPECT_EQ(scene2->GetName(), "EmptyName");
  EXPECT_EQ(scene3->GetName(), "Scene With Spaces");
  EXPECT_TRUE(scene1->IsEmpty());
  EXPECT_EQ(scene1->GetNodeCount(), 0);
}

NOLINT_TEST_F(SceneBasicTest, SceneNameOperations)
{
  // Arrange: scene_ is set up by the fixture with an initial name
  // "TestScene".
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
// Error/Assertion/Death Tests
// -----------------------------------------------------------------------------

class SceneBasicErrorTest : public SceneBasicTest { };

NOLINT_TEST_F(
  SceneBasicErrorTest, GetFirstChild_ChildNotScene_LazyInvalidatesNode)
{
  // Arrange: The only way to create this scenario is to hack the node to make
  // it inconsistent.
  auto parent = CreateNode("Parent");
  auto child = scene_->CreateChildNode(parent, "Child");
  // Save the child handle
  const auto child_handle = child->GetHandle();

  // Destroy the child.
  scene_->DestroyNodeHierarchy(*child);
  EXPECT_FALSE(parent.HasChildren());
  EXPECT_TRUE(parent.IsValid());

  // Act: Hack the parent to set its first child to a no longer existing node,
  // bypassing the Scene.
  auto parent_impl = parent.GetObject();
  parent_impl->get().AsGraphNode().SetFirstChild(child_handle);
  const auto first_child = scene_->GetFirstChild(parent);

  // Assert: nullopt returned, and parent now invalidated (and a lost parent
  // impl in the table until we destroy the scene, expected as we hacked the
  // graph).
  EXPECT_FALSE(first_child.has_value());
  EXPECT_FALSE(parent.IsValid()); // lazily invalidated
  EXPECT_EQ(scene_->GetNodeCount(), 1); // Messed-up scene
}

NOLINT_TEST_F(
  SceneBasicErrorTest, GetFirstChild_DanglingNode_LazyInvalidatesNode)
{
  // Arrange: The most reasonable way to recreate this situation is to destroy a
  // hierarchy and then attempt to act on a node that is not the hierarchy
  // starting node.
  auto parent = CreateNode("Parent");
  auto destroy_root = scene_->CreateChildNode(parent, "DestroyRoot");
  auto child = scene_->CreateChildNode(*destroy_root, "DanglingChild");
  auto grandchild = scene_->CreateChildNode(*child, "DanglingGrandChild");
  EXPECT_TRUE(child.has_value());
  EXPECT_TRUE(child->IsValid());
  EXPECT_TRUE(child->HasChildren());

  // Destroy the hierarchy.
  scene_->DestroyNodeHierarchy(*destroy_root);
  EXPECT_TRUE(child.value().IsValid()); // still valid
  EXPECT_TRUE(child->IsValid());

  // Act: Use the dangling child
  const auto first_child = scene_->GetFirstChild(*child);

  // Assert:
  EXPECT_FALSE(first_child.has_value());
  EXPECT_FALSE(child->IsValid()); // lazily invalidated
}

NOLINT_TEST_F(SceneBasicErrorTest, GetFirstChild_InvalidHandle_ReturnsNullOpt)
{
  // Arrange:
  SceneNode invalid_node = CreateNodeWithInvalidHandle();

  // Act:
  const auto child = scene_->GetFirstChild(invalid_node);

  // Assert:
  EXPECT_FALSE(child.has_value());
}

NOLINT_TEST_F(SceneBasicErrorTest, GetParent_InvalidHandle_ReturnsNullOpt)
{
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act:
  const auto parent = scene_->GetParent(invalid_node);

  // Assert:
  EXPECT_FALSE(parent.has_value());
}

NOLINT_TEST_F(SceneBasicErrorTest, GetNextSibling_InvalidHandle_ReturnsNullOpt)
{
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act:
  const auto sibling = scene_->GetNextSibling(invalid_node);

  // Assert:
  EXPECT_FALSE(sibling.has_value());
}

NOLINT_TEST_F(SceneBasicErrorTest, GetPrevSibling_InvalidHandle_ReturnsNullOpt)
{
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act:
  const auto sibling = scene_->GetPrevSibling(invalid_node);

  // Assert:
  EXPECT_FALSE(sibling.has_value());
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

NOLINT_TEST_F(SceneBasicTest, ContainsNodeFromDifferentScene)
{
  // Arrange: Create a node in a separate, different scene.
  const auto other_scene = std::make_shared<Scene>("OtherScene", 1024);
  const auto other_node = other_scene->CreateNode("OtherNode");

  // Assert: Verify the current scene does not contain the foreign
  // node/handle, while the other scene correctly reports containment.
  EXPECT_FALSE(scene_->Contains(other_node));
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
  ClearScene(); // Assert: Verify scene is empty, node count is zero, and all
                // previously
  // created nodes are invalidated and not contained.
  EXPECT_EQ(scene_->GetNodeCount(), 0);
  TRACE_GCHECK_F(ExpectSceneEmpty(), "empty")
  TRACE_GCHECK_F(ExpectNodeNotInScene(parent), "parent-scene")
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(parent), "parent-invalid")
  if (child1_opt.has_value()) {
    TRACE_GCHECK_F(ExpectNodeNotInScene(child1_opt.value()), "child1-scene")
    TRACE_GCHECK_F(
      ExpectNodeLazyInvalidated(child1_opt.value()), "child1-invalid")
  }
  if (child2_opt.has_value()) {
    TRACE_GCHECK_F(ExpectNodeNotInScene(child2_opt.value()), "child2-scene")
    TRACE_GCHECK_F(
      ExpectNodeLazyInvalidated(child2_opt.value()), "child2-invalid")
  }
  TRACE_GCHECK_F(ExpectNodeNotInScene(standalone), "standalone-scene")
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(standalone), "standalone-invalid")
}

// -----------------------------------------------------------------------------
// Scene Defragmentation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, DefragmentStorage)
{
  // Arrange: Create three nodes, destroy the middle one to induce
  // fragmentation, and verify node count.
  const auto node1 = CreateNode("Node1");
  auto node2 = CreateNode("Node2");
  const auto node3 = CreateNode("Node3");
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
  const std::string long_name(1000, 'A');

  // Act: Create a node using the prepared very long name.
  auto node = CreateNode(long_name);

  // Assert: Verify the node is valid and its name is correctly stored and
  // retrieved, matching the long string.
  EXPECT_TRUE(node.IsValid());
  const auto obj = node.GetObject();
  ASSERT_TRUE(obj.has_value());
  EXPECT_EQ(obj->get().GetName(), long_name);
}

NOLINT_TEST_F(SceneBasicTest, UnicodeCharacterNames)
{
  // Arrange: Scene is ready for node creation (fixture setup).

  // Act: Create nodes with names containing various Unicode characters (e.g.,
  // Japanese, Cyrillic, Emojis).
  auto node1 = CreateNode("Node_こんにちは");
  // ReSharper disable once StringLiteralTypo
  auto node2 = CreateNode("Node_Здравствуй");
  auto node3 = CreateNode("Node_🚀🌟");

  // Assert: Verify all nodes are valid and their names are correctly stored
  // and retrieved, preserving Unicode characters.
  EXPECT_TRUE(node1.IsValid());
  EXPECT_TRUE(node2.IsValid());
  EXPECT_TRUE(node3.IsValid());
  const auto obj1 = node1.GetObject();
  const auto obj2 = node2.GetObject();
  const auto obj3 = node3.GetObject();
  ASSERT_TRUE(obj1.has_value());
  ASSERT_TRUE(obj2.has_value());
  ASSERT_TRUE(obj3.has_value());
  EXPECT_EQ(obj1->get().GetName(), "Node_こんにちは");
  // ReSharper disable once StringLiteralTypo
  EXPECT_EQ(obj2->get().GetName(), "Node_Здравствуй");
  EXPECT_EQ(obj3->get().GetName(), "Node_🚀🌟");
}

} // namespace
