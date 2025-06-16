//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include "./SceneTest.h"
#include "Helpers/TestSceneFactory.h"
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
using oxygen::scene::testing::TestSceneFactory;

namespace {

//=============================================================================
// Scene Basic Functionality Tests
//=============================================================================

class SceneBasicTest : public oxygen::scene::testing::SceneTest { };

// -----------------------------------------------------------------------------
// Scene Construction and Metadata Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneBasicTest, SceneConstruction)
{
  // Arrange: No specific arrangement beyond fixture setup.

  // Act: Create three separate Scene instances with different names using
  // factory.
  auto& factory = TestSceneFactory::Instance();
  const auto scene1 = factory.CreateSingleNodeScene("Scene1");
  const auto scene2 = factory.CreateSingleNodeScene("EmptyName");
  const auto scene3 = factory.CreateSingleNodeScene("Scene With Spaces");

  // Assert: Verify names are set correctly and scenes have expected structure.
  EXPECT_EQ(scene1->GetName(), "Scene1");
  EXPECT_EQ(scene2->GetName(), "EmptyName");
  EXPECT_EQ(scene3->GetName(), "Scene With Spaces");
  EXPECT_FALSE(scene1->IsEmpty()); // Has one node from factory
  EXPECT_EQ(scene1->GetNodeCount(), 1);
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
  // Arrange: Create a node in a separate, different scene using
  // TestSceneFactory.
  auto& factory = TestSceneFactory::Instance();
  const auto other_scene = factory.CreateSingleNodeScene("OtherScene");
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
  // Arrange: Create a structured hierarchy using TestSceneFactory and add a
  // standalone node.
  auto& factory = TestSceneFactory::Instance();
  auto test_scene = factory.CreateParentWithChildrenScene("TestScene", 2);

  // Replace our fixture scene with the factory-created one for this test
  scene_ = test_scene;

  // Add a standalone node to the factory-created scene
  auto standalone = CreateNode("Standalone");
  EXPECT_EQ(scene_->GetNodeCount(), 4); // Parent + 2 children + standalone
  EXPECT_FALSE(scene_->IsEmpty());

  // Act: Clear the entire scene.
  ClearScene();
  // Assert: Verify scene is empty, node count is zero, and all previously
  // created nodes are invalidated and not contained.
  EXPECT_EQ(scene_->GetNodeCount(), 0);
  EXPECT_TRUE(scene_->IsEmpty());
  EXPECT_FALSE(scene_->Contains(standalone));
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(standalone), "standalone-invalid");
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
  TRACE_GCHECK_F(ExpectNodeWithName(node1, "Node@#$%"), "node1-special-chars");
  TRACE_GCHECK_F(ExpectNodeWithName(node2, "Node With Spaces"), "node2-spaces");
  TRACE_GCHECK_F(ExpectNodeWithName(node3, "Node\tWith\nSpecial\rChars"),
    "node3-control-chars");
  TRACE_GCHECK_F(
    ExpectNodeWithName(node4, "Node_with-symbols.123"), "node4-symbols");
}

NOLINT_TEST_F(SceneBasicTest, VeryLongNodeNames)
{
  // Arrange: Prepare a very long string to be used as a node name.
  const std::string long_name(1000, 'A');

  // Act: Create a node using the prepared very long name.
  auto node = CreateNode(long_name);

  // Assert: Verify the node is valid and its name is correctly stored and
  // retrieved, matching the long string.
  TRACE_GCHECK_F(ExpectNodeWithName(node, long_name), "long-name-validation");
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
  TRACE_GCHECK_F(
    ExpectNodeWithName(node1, "Node_こんにちは"), "japanese-chars");

  // ReSharper disable once StringLiteralTypo
  TRACE_GCHECK_F(
    ExpectNodeWithName(node2, "Node_Здравствуй"), "cyrillic-chars");
  TRACE_GCHECK_F(ExpectNodeWithName(node3, "Node_🚀🌟"), "emoji-chars");
}

} // namespace
