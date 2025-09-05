//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <fmt/format.h>

#include <Oxygen/Testing/GTest.h>

#include "./SceneTest.h"
#include "Helpers/TestSceneFactory.h"
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::ObjectMetadata;
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

class SceneAsNodeFactoryTest : public oxygen::scene::testing::SceneTest {
protected:
  auto CreateParentChildPair(const std::string& parent_name,
    const std::string& child_name) const -> std::pair<SceneNode, SceneNode>
  {
    auto parent = CreateNode(parent_name);
    auto child_opt = CreateChildNode(parent, child_name);
    EXPECT_TRUE(child_opt.has_value());
    return std::make_pair(parent, child_opt.value());
  }

  // Helper method for creating test scenes with TestSceneFactory (removed
  // validation)
  auto CreateTestSceneWithFactory(const std::string& scene_name,
    int child_count = 1) const -> std::shared_ptr<Scene>
  {
    auto& factory = TestSceneFactory::Instance();
    return child_count == 1
      ? factory.CreateSingleNodeScene(scene_name)
      : factory.CreateParentWithChildrenScene(scene_name, child_count);
  }

  static void ExpectHandlesUnique(
    const SceneNode& n1, const SceneNode& n2, const SceneNode& n3)
  {
    EXPECT_NE(n1.GetHandle(), n2.GetHandle())
      << "Node handles should be unique (n1 vs n2)";
    EXPECT_NE(n2.GetHandle(), n3.GetHandle())
      << "Node handles should be unique (n2 vs n3)";
    EXPECT_NE(n1.GetHandle(), n3.GetHandle())
      << "Node handles should be unique (n1 vs n3)";
  }
};

//=============================================================================
// SceneAsNodeFactoryTest - Basic Functionality Tests
//=============================================================================

// -----------------------------------------------------------------------------
// Node Creation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateNode_BasicName_Succeeds)
{
  // Arrange: Scene is ready for use (fixture setup).

  // Act: Create a single node with a specific name.
  auto node = CreateNode("TestNode");
  // Assert: Verify the node is valid, has the correct name, and scene
  // statistics are updated.
  TRACE_GCHECK_F(ExpectNodeWithName(node, "TestNode"), "node-valid-name");
  EXPECT_EQ(scene_->GetNodeCount(), 1);
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateNode_EmptyName_Succeeds)
{
  // Arrange: Scene is ready for use (fixture setup).

  // Act: Create a node with an empty name.
  auto node = CreateNode("");
  // Assert: Node should be valid and have an empty name.
  TRACE_GCHECK_F(ExpectNodeWithName(node, ""), "empty-name-node");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateNode_WithCustomFlags_Succeeds)
{
  // Arrange: Define custom node flags (e.g., not visible, static).
  const auto custom_flags = SceneNode::Flags {}
                              .SetFlag(SceneNodeFlags::kVisible,
                                SceneFlag {}.SetEffectiveValueBit(false))
                              .SetFlag(SceneNodeFlags::kStatic,
                                SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Create a node with the specified custom flags.
  auto node = CreateNode("FlaggedNode", custom_flags);

  // Assert: Verify the node is valid and its flags match the custom flags set.
  EXPECT_TRUE(node.IsValid());
  const auto flags_opt = node.GetFlags();
  ASSERT_TRUE(flags_opt.has_value());
  const auto& flags = flags_opt->get();
  EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_TRUE(flags.GetEffectiveValue(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateNode_Multiple_Succeeds)
{
  // Arrange: Scene is ready for use (fixture setup).

  // Act: Create three distinct nodes.
  const auto node1 = CreateNode("Node1");
  const auto node2 = CreateNode("Node2");
  const auto node3 = CreateNode("Node3");

  // Assert: All nodes should be valid, their handles unique, and the count
  // updated correctly.
  EXPECT_TRUE(node1.IsValid());
  EXPECT_TRUE(node2.IsValid());
  EXPECT_TRUE(node3.IsValid());
  EXPECT_EQ(scene_->GetNodeCount(), 3);
  TRACE_GCHECK_F(ExpectHandlesUnique(node1, node2, node3), "unique-handles");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateChildNode_BasicParent_Succeeds)
{
  // Arrange: Create parent-child hierarchy
  const auto [parent, child] = CreateParentChildPair("Parent", "Child");

  // Act: (hierarchy creation is part of arrange)

  // Assert: Verify the relationship is established correctly
  TRACE_GCHECK_F(ExpectNodeWithName(parent, "Parent"), "parent-valid");
  TRACE_GCHECK_F(ExpectNodeWithName(child, "Child"), "child-valid");
  EXPECT_EQ(scene_->GetNodeCount(), 2);
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateChildNode_WithCustomFlags_Succeeds)
{
  // Arrange: Create a parent node and define custom flags for child
  auto parent = CreateNode("Parent");
  const auto custom_flags = SceneNode::Flags {}
                              .SetFlag(SceneNodeFlags::kVisible,
                                SceneFlag {}.SetEffectiveValueBit(false))
                              .SetFlag(SceneNodeFlags::kStatic,
                                SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Create a child node with custom flags
  auto child_opt = scene_->CreateChildNode(parent, "CustomChild", custom_flags);

  // Assert: Verify child was created with correct flags
  ASSERT_TRUE(child_opt.has_value());
  auto& child = child_opt.value();
  TRACE_GCHECK_F(ExpectNodeWithName(child, "CustomChild"), "child-valid");

  const auto flags_opt = child.GetFlags();
  ASSERT_TRUE(flags_opt.has_value());
  const auto& flags = flags_opt->get();
  EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_TRUE(flags.GetEffectiveValue(SceneNodeFlags::kStatic));
  EXPECT_EQ(scene_->GetNodeCount(), 2);
}

// -----------------------------------------------------------------------------
// Node Destruction Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryTest, DestroyNode_SingleNode_Succeeds)
{
  // Arrange: Create a single node
  auto node = CreateNode("NodeToDestroy");
  EXPECT_EQ(scene_->GetNodeCount(), 1);

  // Act: Destroy the created node
  const bool destroyed = DestroyNode(node);

  // Assert: Verify successful destruction and scene state
  EXPECT_TRUE(destroyed);
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(node), "node-invalidated");
  TRACE_GCHECK_F(ExpectSceneEmpty(), "scene-empty");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchy_ParentWithChildren_Succeeds)
{
  // Arrange: Create a parent node with two children
  auto parent = CreateNode("Parent");
  const auto child1_opt = CreateChildNode(parent, "Child1");
  const auto child2_opt = CreateChildNode(parent, "Child2");
  ASSERT_TRUE(child1_opt.has_value() && child2_opt.has_value());
  auto child1 = child1_opt.value();
  auto child2 = child2_opt.value();
  EXPECT_EQ(scene_->GetNodeCount(), 3);

  // Act: Destroy the parent node and its entire hierarchy
  const bool destroyed = DestroyNodeHierarchy(parent);

  // Assert: Verify complete hierarchy destruction
  EXPECT_TRUE(destroyed);
  TRACE_GCHECK_F(ExpectSceneEmpty(), "scene-empty");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(parent), "parent-invalid");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(child1), "child1-invalid");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(child2), "child2-invalid");
}

//=============================================================================
// SceneAsNodeFactoryErrorTest - Error/Failure Tests
//=============================================================================

// Error/Assertion fixture
class SceneAsNodeFactoryErrorTest : public testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene"); }
  void TearDown() override { scene_.reset(); }

  [[nodiscard]] auto CreateNode(const std::string& name) const -> SceneNode
  {
    return scene_->CreateNode(name);
  }

  [[nodiscard]] auto CreateNodeWithInvalidHandle() const -> SceneNode
  {
    return SceneNode(scene_);
  }

  // Helper to create a to-be lazily invalidated node for testing. Creates a
  // node, stores its handle, then destroys it, and returns a new node with the
  // stored handle.
  [[nodiscard]] auto CreateLazyInvalidationNode(
    const std::string& name = "InvalidNode") const -> SceneNode
  {
    auto node = scene_->CreateNode(name);
    const auto handle = node.GetHandle();
    scene_->DestroyNode(node);
    return { scene_, handle };
  }

  std::shared_ptr<Scene> scene_; // NOLINT(*-non-private-member-*)
};

// -----------------------------------------------------------------------------
// Single Node Destruction Error Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest, DestroyNode_NonExistentNode_Fails)
{
  // Arrange: Create a node then destroy it to make it non-existent
  auto node = CreateLazyInvalidationNode("NonExistentNode");

  // Act: Attempt to destroy the non-existent node
  const auto result = scene_->DestroyNode(node);

  // Assert: Verify the operation fails
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest, DestroyNode_InvalidHandle_Fails)
{
  // Arrange: Create node with an invalid handle
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act: Attempt to destroy the invalid node
  const auto result = scene_->DestroyNode(invalid_node);

  // Assert: Verify the operation fails
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, DestroyNodeHierarchy_InvalidStartingNode_Fails)
{
  // Arrange: Create a node with an invalid handle
  auto invalid_root = CreateNodeWithInvalidHandle();

  // Act: Attempt to destroy hierarchy with invalid root
  const auto result = scene_->DestroyNodeHierarchy(invalid_root);

  // Assert: Verify the operation fails
  EXPECT_FALSE(result);
}

// -----------------------------------------------------------------------------
// Child Node Creation Error Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, CreateChildNode_NonExistentParent_Fails)
{
  // Arrange: Create a node then destroy it
  auto node = CreateNode("Node");
  EXPECT_TRUE(scene_->DestroyNode(node));

  // Act: Attempt to create child for non-existent parent
  const auto child = scene_->CreateChildNode(node, "Child");

  // Assert: Verify the operation fails
  EXPECT_FALSE(child.has_value());
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, CreateChildNode_InvalidParentHandle_Fails)
{
  // Arrange: Create node with an invalid handle
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act: Attempt to create a child node with an invalid parent
  const auto child = scene_->CreateChildNode(invalid_node, "Child");

  // Assert: Verify the operation fails
  EXPECT_FALSE(child.has_value());
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, CreateChildNodeWithFlags_NonExistentParent_Fails)
{
  // Arrange: Create a node then destroy it, and prepare custom flags
  auto node = CreateNode("Node");
  EXPECT_TRUE(scene_->DestroyNode(node));
  const auto custom_flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));

  // Act: Attempt to create child with custom flags for non-existent parent
  const auto child = scene_->CreateChildNode(node, "Child", custom_flags);

  // Assert: Verify the operation fails
  EXPECT_FALSE(child.has_value());
}

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest,
  CreateChildNodeWithFlags_InvalidParentHandle_Fails)
{
  // Arrange: Create node with an invalid handle and custom flags
  auto invalid_node = CreateNodeWithInvalidHandle();
  const auto custom_flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Attempt to create child with custom flags for invalid parent
  const auto child
    = scene_->CreateChildNode(invalid_node, "Child", custom_flags);

  // Assert: Verify the operation fails
  EXPECT_FALSE(child.has_value());
}

// -----------------------------------------------------------------------------
// Batch Operation Error Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, DestroyNodes_WithInvalidNodes_PartialSuccess)
{
  // Arrange: Mix of valid and invalid nodes
  auto valid_node = CreateNode("ValidNode");
  auto invalid_node = CreateNodeWithInvalidHandle();
  auto destroyed_node = CreateLazyInvalidationNode("DestroyedNode");

  std::vector<SceneNode> mixed_nodes
    = { valid_node, invalid_node, destroyed_node };

  // Act: Attempt to destroy a mixed set
  const auto results = scene_->DestroyNodes(mixed_nodes);

  // Assert: Only valid node should be destroyed
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 1); // valid_node destroyed
  EXPECT_EQ(results[1], 0); // invalid_node failed
  EXPECT_EQ(results[2], 0); // destroyed_node failed
}

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest,
  DestroyNodeHierarchies_WithInvalidRoots_PartialSuccess)
{
  // Arrange: Mix of valid and invalid hierarchy roots
  auto valid_root = CreateNode("ValidRoot");
  const auto child_opt = scene_->CreateChildNode(valid_root, "Child");
  ASSERT_TRUE(child_opt.has_value());

  auto invalid_root = CreateNodeWithInvalidHandle();
  auto destroyed_root = CreateLazyInvalidationNode("DestroyedRoot");

  std::vector<SceneNode> mixed_roots
    = { valid_root, invalid_root, destroyed_root };

  // Act: Attempt to destroy mixed hierarchies
  const auto results = scene_->DestroyNodeHierarchies(mixed_roots);

  // Assert: Only valid hierarchy should be destroyed
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 1); // valid_root hierarchy destroyed
  EXPECT_EQ(results[1], 0); // invalid_root failed
  EXPECT_EQ(results[2], 0); // destroyed_root failed
}

//=============================================================================
// SceneAsNodeFactoryDeathTest - Death/Assertion Tests
//=============================================================================

// New Fixture for Death Tests related to Scene basic operations GCHECK_F
// assertions
class SceneAsNodeFactoryDeathTest : public testing::Test {
protected:
  std::shared_ptr<Scene> scene_; // NOLINT(*-non-private-member-*)

  void SetUp() override { scene_ = std::make_shared<Scene>("TestDeathScene"); }

  void TearDown() override { scene_.reset(); }
};

// -----------------------------------------------------------------------------
// Foreign Scene Death Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneAsNodeFactoryDeathTest, CreateChildNode_WithForeignParent_Death)
{
  // Arrange: Create a parent node in another scene.
  const auto other_scene = std::make_shared<Scene>("OtherScene", 1);
  auto foreign_parent = other_scene->CreateNode("ForeignParent");

  // Act and Assert: Attempt to create a child node with a foreign parent
  NOLINT_ASSERT_DEATH([[maybe_unused]] auto _
    = scene_->CreateChildNode(foreign_parent, "BadChild"),
    ".*does not belong to scene.*");
}

NOLINT_TEST_F(SceneAsNodeFactoryDeathTest,
  CreateChildNodeWithCustomFlags_WithForeignParent_Death)
{
  // Arrange: Create a parent node in another scene
  const auto other_scene = std::make_shared<Scene>("OtherScene", 1);
  auto foreign_parent = other_scene->CreateNode("ForeignParent");

  const auto custom_flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));

  // Act and Assert: Attempt to create a child node with custom flags using
  // foreign parent
  NOLINT_ASSERT_DEATH([[maybe_unused]] auto _
    = scene_->CreateChildNode(foreign_parent, "BadChild", custom_flags),
    ".*does not belong to scene.*");
}

// -----------------------------------------------------------------------------
// Node Destruction Death Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryDeathTest, DestroyNode_WithChildren_Death)
{
  // Arrange: Create parent with child
  auto parent = scene_->CreateNode("ParentWithChild");
  ASSERT_TRUE(parent.IsValid());
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value() && child_opt->IsValid());

  // Act and Assert: Attempting to destroy parent with children should trigger
  // assertion
  NOLINT_ASSERT_DEATH(scene_->DestroyNode(parent), ".*has children.*");
}

// -----------------------------------------------------------------------------
// Batch Operation Death Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryDeathTest, DestroyNodes_WithForeignNode_Death)
{
  // Arrange: Create nodes in different scenes
  auto local_node = scene_->CreateNode("LocalNode");
  const auto other_scene = std::make_shared<Scene>("OtherScene", 1);
  auto foreign_node = other_scene->CreateNode("ForeignNode");

  std::vector<SceneNode> mixed_nodes = { local_node, foreign_node };

  // Act and Assert: Should terminate when trying to destroy foreign node
  NOLINT_ASSERT_DEATH(
    [[maybe_unused]] auto _ = scene_->DestroyNodes(mixed_nodes),
    ".*does not belong to scene.*");
}

NOLINT_TEST_F(SceneAsNodeFactoryDeathTest,
  DestroyNodeHierarchies_WithForeignHierarchy_Death)
{
  // Arrange: Create hierarchies in different scenes
  auto local_root = scene_->CreateNode("LocalRoot");
  const auto other_scene = std::make_shared<Scene>("OtherScene", 1);
  auto foreign_root = other_scene->CreateNode("ForeignRoot");

  std::vector<SceneNode> mixed_roots = { local_root, foreign_root };

  // Act and Assert: Should terminate when trying to destroy foreign hierarchy
  NOLINT_ASSERT_DEATH(
    [[maybe_unused]] auto _ = scene_->DestroyNodeHierarchies(mixed_roots),
    ".*does not belong to scene.*");
}

// -----------------------------------------------------------------------------
// Batch Operation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodes_MultipleSeparateNodes_Succeeds)
{
  // Arrange: Create multiple leaf nodes
  auto node1 = CreateNode("Node1");
  auto node2 = CreateNode("Node2");
  auto node3 = CreateNode("Node3");
  EXPECT_EQ(scene_->GetNodeCount(), 3);
  std::vector<SceneNode> nodes_to_destroy = { node1, node2, node3 };

  // Act: Destroy all nodes in batch
  const auto results = scene_->DestroyNodes(nodes_to_destroy);

  // Assert: Verify all destructions succeeded
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 1); // node1 destroyed
  EXPECT_EQ(results[1], 1); // node2 destroyed
  EXPECT_EQ(results[2], 1); // node3 destroyed
  TRACE_GCHECK_F(ExpectSceneEmpty(), "scene-empty");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(node1), "node1-invalid");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(node2), "node2-invalid");
  TRACE_GCHECK_F(ExpectNodeLazyInvalidated(node3), "node3-invalid");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, DestroyNodes_EmptySpan_Succeeds)
{
  // Arrange: Empty span of nodes
  std::vector<SceneNode> empty_nodes;

  // Act: Destroy empty span
  const auto results = scene_->DestroyNodes(empty_nodes);

  // Assert: Should return empty result vector
  EXPECT_TRUE(results.empty());
  TRACE_GCHECK_F(ExpectSceneEmpty(), "scene-remains-empty");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchies_MultipleHierarchies_Succeeds)
{
  // Arrange: Create multiple hierarchies using factory
  auto& factory = TestSceneFactory::Instance();
  scene_ = factory.CreateForestScene(
    "MultiHierarchy", 3, 2); // 3 roots, 2 children each
  auto hierarchy_roots = scene_->GetRootNodes();

  // Act: Destroy all hierarchies in batch
  const auto results = scene_->DestroyNodeHierarchies(hierarchy_roots);

  // Assert: Verify complete destruction
  TRACE_GCHECK_F(ExpectSceneEmpty(), "forest-destroyed");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, DestroyNodeHierarchies_EmptySpan_Succeeds)
{
  // Arrange: Empty span of hierarchy roots
  std::vector<SceneNode> empty_hierarchies;

  // Act: Destroy empty span
  const auto results = scene_->DestroyNodeHierarchies(empty_hierarchies);

  // Assert: Should return empty result vector
  EXPECT_TRUE(results.empty());
  TRACE_GCHECK_F(ExpectSceneEmpty(), "scene-remains-empty");
}

// -----------------------------------------------------------------------------
// Statistics and Complex Hierarchy Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryTest, NodeStatistics_ThroughOperations_Succeeds)
{
  // Arrange: Start with an empty scene
  TRACE_GCHECK_F(ExpectSceneEmpty(), "start-empty");

  // Act & Assert: Create nodes and verify counts incrementally
  auto node1 = CreateNode("Node1");
  EXPECT_EQ(scene_->GetNodeCount(), 1);
  EXPECT_FALSE(scene_->IsEmpty());

  const auto node2 = CreateNode("Node2");
  EXPECT_EQ(scene_->GetNodeCount(), 2);

  const auto child_opt = CreateChildNode(node1, "Child");
  ASSERT_TRUE(child_opt.has_value());
  EXPECT_EQ(scene_->GetNodeCount(), 3);

  // Act & Assert: Destroy individual node
  auto child = child_opt.value();
  const bool destroyed = DestroyNode(child);
  EXPECT_TRUE(destroyed);
  EXPECT_EQ(scene_->GetNodeCount(), 2);

  // Act & Assert: Destroy hierarchy
  const bool hierarchy_destroyed = DestroyNodeHierarchy(node1);
  EXPECT_TRUE(hierarchy_destroyed);
  EXPECT_EQ(scene_->GetNodeCount(), 1);

  // Act & Assert: Clear remaining node
  auto node2_copy = node2;
  const bool last_destroyed = DestroyNode(node2_copy);
  EXPECT_TRUE(last_destroyed);
  TRACE_GCHECK_F(ExpectSceneEmpty(), "final-empty");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchy_LargeComplexHierarchy_Succeeds)
{
  // Arrange: Create a complex binary tree hierarchy using TestSceneFactory
  auto& factory = TestSceneFactory::Instance();
  scene_ = factory.CreateBinaryTreeScene("BinaryTree", 3);
  auto roots = scene_->GetRootNodes();
  ASSERT_FALSE(roots.empty());
  auto root = roots[0];

  // Act: Destroy the entire complex hierarchy
  const bool destroyed = DestroyNodeHierarchy(root);

  // Assert: Verify complete destruction
  EXPECT_TRUE(destroyed);
  TRACE_GCHECK_F(ExpectSceneEmpty(), "binary-tree-destroyed");
}

} // namespace
