//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <fmt/format.h>

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

class SceneAsNodeFactoryTest : public testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene"); }
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

  [[nodiscard]] auto CreateChildNode(const SceneNode& parent,
    const std::string& name) const -> std::optional<SceneNode>
  {
    return scene_->CreateChildNode(parent, name);
  }

  auto DestroyNode(SceneNode& node) const -> bool
  {
    return scene_->DestroyNode(node);
  }

  auto DestroyNodeHierarchy(SceneNode& node) const -> bool
  {
    return scene_->DestroyNodeHierarchy(node);
  }
  static void ExpectNodeValidWithName(
    const SceneNode& node, const std::string& name)
  {
    EXPECT_TRUE(node.IsValid()) << "Node should be valid";
    const auto obj_opt = node.GetObject();
    EXPECT_TRUE(obj_opt.has_value()) << "Node object should be present";
    if (obj_opt.has_value()) {
      EXPECT_EQ(obj_opt->get().GetName(), name)
        << "Node name mismatch: expected '" << name << "', got '"
        << obj_opt->get().GetName() << "'";
    }
  }
  static void ExpectNodeLazyInvalidated(SceneNode& node)
  {
    // Node may appear valid, but after GetObject() it should be invalidated
    if (node.IsValid()) {
      const auto obj_opt = node.GetObject();
      EXPECT_FALSE(obj_opt.has_value())
        << "Node should not have a valid object after destruction/clear";
      EXPECT_FALSE(node.IsValid())
        << "Node should be invalidated after failed access (lazy invalidation)";
    }
  }
  void ExpectNodeNotInScene(const SceneNode& node) const
  {
    EXPECT_FALSE(scene_->Contains(node))
      << "Node should not be contained in scene";
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
  void ExpectSceneEmpty() const
  {
    EXPECT_TRUE(scene_->IsEmpty()) << "Scene should be empty";
    EXPECT_EQ(scene_->GetNodeCount(), 0) << "Scene node count should be zero";
  }
  std::shared_ptr<Scene> scene_; // NOLINT(*-non-private-member-*)
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
  const auto node = CreateNode("TestNode");
  // Assert: Verify the node is valid, has the correct name, and scene
  // statistics are updated.
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(node, "TestNode"),
    "expecting node to be valid with correct name");
  EXPECT_EQ(scene_->GetNodeCount(), 1);
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateNode_EmptyName_Succeeds)
{
  // Arrange: Scene is ready for use (fixture setup).

  // Act: Create a node with an empty name.
  const auto node = CreateNode("");
  // Assert: Node should be valid and have an empty name.
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(node, ""),
    "expecting node to be valid with empty name");
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
  CHECK_FOR_FAILURES_MSG(ExpectHandlesUnique(node1, node2, node3),
    "expecting node handles to be unique");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateChildNode_BasicParent_Succeeds)
{
  // Arrange: Create a parent node and verify its validity.
  // Scene graph: Parent -> Child
  const auto parent = CreateNode("Parent");
  EXPECT_TRUE(parent.IsValid());

  // Act: Create a child node for the previously created parent.
  const auto child_opt = CreateChildNode(parent, "Child");

  // Assert: Verify the child was created, both parent and child are valid
  // with correct names, and scene node count is updated.
  // ASSERT_TRUE(child_opt.has_value());
  const auto& child = child_opt.value();
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(parent, "Parent"),
    "expecting parent node to be valid with correct name");
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(child, "Child"),
    "expecting child node to be valid with correct name");
  EXPECT_EQ(scene_->GetNodeCount(), 2);
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, CreateChildNode_WithCustomFlags_Succeeds)
{
  // Arrange: Create a parent node and define custom flags for child
  // Scene graph: Parent -> CustomChild
  const auto parent = CreateNode("Parent");
  EXPECT_TRUE(parent.IsValid());

  const auto custom_flags = SceneNode::Flags {}
                              .SetFlag(SceneNodeFlags::kVisible,
                                SceneFlag {}.SetEffectiveValueBit(false))
                              .SetFlag(SceneNodeFlags::kStatic,
                                SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Create a child node with custom flags
  const auto child_opt
    = scene_->CreateChildNode(parent, "CustomChild", custom_flags);

  // Assert: Verify child was created with correct flags
  // ASSERT_TRUE(child_opt.has_value());
  const auto& child = child_opt.value();
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(child, "CustomChild"),
    "expecting child node to be valid with correct name");

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
  // Arrange: Create a single node and verify its initial valid state and
  // scene count.
  auto node = CreateNode("NodeToDestroy");
  EXPECT_TRUE(node.IsValid());
  EXPECT_EQ(scene_->GetNodeCount(), 1);

  // Act: Destroy the created node.
  const bool destroyed = DestroyNode(node);
  // Assert: Verify successful destruction, node invalidation, and scene
  // emptiness.
  EXPECT_TRUE(destroyed);
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(node),
    "expecting node to be invalidated after destruction");
  CHECK_FOR_FAILURES_MSG(
    ExpectSceneEmpty(), "expecting scene to be empty after node destruction");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchy_ParentWithChildren_Succeeds)
{
  // Arrange: Create a parent node and two child nodes.
  // Scene graph:
  //   Parent
  //   ├── Child1
  //   └── Child2
  auto parent = CreateNode("Parent");
  const auto child1_opt = CreateChildNode(parent, "Child1");
  const auto child2_opt = CreateChildNode(parent, "Child2");
  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  auto child1 = child1_opt.value();
  auto child2 = child2_opt.value();
  EXPECT_EQ(scene_->GetNodeCount(), 3);

  // Act: Destroy the parent node and its entire hierarchy.
  const bool destroyed = DestroyNodeHierarchy(parent);
  // Assert: Verify successful destruction, scene emptiness, and invalidation
  // of parent and all children.
  EXPECT_TRUE(destroyed);
  CHECK_FOR_FAILURES_MSG(ExpectSceneEmpty(),
    "expecting scene to be empty after hierarchy destruction");
  CHECK_FOR_FAILURES_MSG(
    ExpectNodeNotInScene(parent), "expecting parent node not to be in scene");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(parent),
    "expecting parent node to be invalidated");
  CHECK_FOR_FAILURES_MSG(
    ExpectNodeNotInScene(child1), "expecting child1 node not to be in scene");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(child1),
    "expecting child1 node to be invalidated");
  CHECK_FOR_FAILURES_MSG(
    ExpectNodeNotInScene(child2), "expecting child2 node not to be in scene");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(child2),
    "expecting child2 node to be invalidated");
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
  // Arrange: Create a node then destroy it, making it non-existent.
  auto node = CreateLazyInvalidationNode("NonExistentNode");

  // Act: Destroy the node, making it non-existent for a later operation.
  const auto result = scene_->DestroyNode(node);

  // Assert: Verify the result is false, indicating failed destruction.
  EXPECT_FALSE(result) << "Destroying a non-existent node should return false";
}

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest, DestroyNode_InvalidHandle_Fails)
{
  // Arrange: Create node with an invalid handle
  auto invalid_node = CreateNodeWithInvalidHandle();

  // Act: Attempt to destroy the invalid node
  const auto result = scene_->DestroyNode(invalid_node);

  // Assert: Verify the result is false, indicating failed destruction.
  EXPECT_FALSE(result) << "Destroying an invalid node should return false";
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, DestroyNodeHierarchy_InvalidStartingNode_Fails)
{
  // Arrange: Create a node with an invalid handle.
  SceneNode invalid_root = CreateNodeWithInvalidHandle();

  // Act: Attempt to destroy hierarchy with invalid root
  const auto result = scene_->DestroyNodeHierarchy(invalid_root);

  // Assert: operation should fail, returning false.
  EXPECT_FALSE(result) << "Destroying a hierarchy starting with an invalid "
                          "node should return false";
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

  // Assert: Should fail
  EXPECT_FALSE(child.has_value())
    << "Creating a child for a non-existent node should return nullopt";
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, CreateChildNode_InvalidParentHandle_Fails)
{
  // Arrange: Create node with an invalid handle
  const auto invalid_node = CreateNodeWithInvalidHandle();

  // Act: Attempt to create a child node with an invalid parent.
  const auto child = scene_->CreateChildNode(invalid_node, "Child");

  // Assert: Verify the result is false, indicating failed creation.
  EXPECT_FALSE(child.has_value())
    << "Creating a child for an invalid node should return nullopt";
}

NOLINT_TEST_F(
  SceneAsNodeFactoryErrorTest, CreateChildNodeWithFlags_NonExistentParent_Fails)
{
  // Arrange: Create a node then destroy it
  auto node = CreateNode("Node");
  EXPECT_TRUE(scene_->DestroyNode(node));

  const auto custom_flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));

  // Act: Attempt to create child with custom flags for non-existent parent
  const auto child = scene_->CreateChildNode(node, "Child", custom_flags);

  // Assert: Should fail
  EXPECT_FALSE(child.has_value()) << "Creating a child with flags for a "
                                     "non-existent node should return nullopt";
}

NOLINT_TEST_F(SceneAsNodeFactoryErrorTest,
  CreateChildNodeWithFlags_InvalidParentHandle_Fails)
{
  // Arrange: Create node with an invalid handle and custom flags
  const auto invalid_node = CreateNodeWithInvalidHandle();
  const auto custom_flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Attempt to create child with custom flags for invalid parent
  const auto child
    = scene_->CreateChildNode(invalid_node, "Child", custom_flags);

  // Assert: Should fail
  EXPECT_FALSE(child.has_value())
    << "Creating a child with flags for an invalid node should return nullopt";
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

// New Fixture for Death Tests related to Scene basic operations CHECK_F
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
  const auto foreign_parent = other_scene->CreateNode("ForeignParent");

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
  const auto foreign_parent = other_scene->CreateNode("ForeignParent");

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
  // Scene graph: ParentWithChild -> Child
  auto parent = scene_->CreateNode("ParentWithChild");
  ASSERT_TRUE(parent.IsValid()); // Ensure parent is valid
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value()
    && child_opt->IsValid()); // Ensure child is valid and created

  // Act and Assert: This should trigger: CHECK_F(!node.HasChildren(), "node has
  // children, use DestroyNodeHierarchy() instead");
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

  // Assert: Destruction should succeed
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 1); // node1 destroyed
  EXPECT_EQ(results[1], 1); // node2 destroyed
  EXPECT_EQ(results[2], 1); // node3 destroyed
  CHECK_FOR_FAILURES_MSG(
    ExpectSceneEmpty(), "expecting scene to be empty after batch destruction");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(node1),
    "expecting node1 to be invalidated after destruction");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(node2),
    "expecting node2 to be invalidated after destruction");
  CHECK_FOR_FAILURES_MSG(ExpectNodeLazyInvalidated(node3),
    "expecting node3 to be invalidated after destruction");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, DestroyNodes_EmptySpan_Succeeds)
{
  // Arrange: Empty span of nodes
  std::vector<SceneNode> empty_nodes;

  // Act: Destroy empty span
  const auto results = scene_->DestroyNodes(empty_nodes);
  // Assert: Should return empty result vector
  EXPECT_TRUE(results.empty());
  CHECK_FOR_FAILURES_MSG(
    ExpectSceneEmpty(), "expecting scene to remain empty with empty node span");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchies_MultipleHierarchies_Succeeds)
{
  // Arrange: Create multiple hierarchies
  // Scene graph:
  //   Root1        Root2           Root3
  //   └── Child1   ├── Child2
  //                └── Grandchild2
  auto root1 = CreateNode("Root1");
  const auto child1_opt = CreateChildNode(root1, "Child1");
  ASSERT_TRUE(child1_opt.has_value());

  auto root2 = CreateNode("Root2");
  const auto child2_opt = CreateChildNode(root2, "Child2");
  const auto grandchild2_opt
    = CreateChildNode(child2_opt.value(), "Grandchild2");
  ASSERT_TRUE(child2_opt.has_value());
  ASSERT_TRUE(grandchild2_opt.has_value());

  auto root3 = CreateNode("Root3"); // Single node hierarchy

  EXPECT_EQ(scene_->GetNodeCount(), 6);
  std::vector<SceneNode> hierarchy_roots = { root1, root2, root3 };

  // Act: Destroy all hierarchies in batch
  const auto results = scene_->DestroyNodeHierarchies(hierarchy_roots);

  // Assert: Destruction should succeed
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 1); // root1 hierarchy destroyed
  EXPECT_EQ(results[1], 1); // root2 hierarchy destroyed
  EXPECT_EQ(results[2], 1); // root3 hierarchy destroyed
  CHECK_FOR_FAILURES_MSG(ExpectSceneEmpty(),
    "expecting scene to be empty after batch hierarchy destruction");
}

NOLINT_TEST_F(SceneAsNodeFactoryTest, DestroyNodeHierarchies_EmptySpan_Succeeds)
{
  // Arrange: Empty span of hierarchy roots
  std::vector<SceneNode> empty_hierarchies;

  // Act: Destroy empty span
  const auto results = scene_->DestroyNodeHierarchies(empty_hierarchies);
  // Assert: Should return empty result vector
  EXPECT_TRUE(results.empty());
  CHECK_FOR_FAILURES_MSG(ExpectSceneEmpty(),
    "expecting scene to remain empty with empty hierarchy span");
}

// -----------------------------------------------------------------------------
// Statistics and Complex Hierarchy Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneAsNodeFactoryTest, NodeStatistics_ThroughOperations_Succeeds)
{
  // Arrange: Start with an empty scene
  CHECK_FOR_FAILURES_MSG(ExpectSceneEmpty(), "expecting scene to start empty");

  // Act & Assert: Create nodes and verify counts
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
  CHECK_FOR_FAILURES_MSG(
    ExpectSceneEmpty(), "expecting scene to be empty after all operations");
}

NOLINT_TEST_F(
  SceneAsNodeFactoryTest, DestroyNodeHierarchy_LargeComplexHierarchy_Succeeds)
{
  // Arrange: Create a deep hierarchy (4 levels of children, 9 nodes total)
  // Scene graph:
  //   Root
  //   ├── Level0_Child1
  //   │   ├── Level1_Child1
  //   │   │   ├── Level2_Child1
  //   │   │   │   ├── Level3_Child1
  //   │   │   │   └── Level3_Child2
  //   │   │   └── Level2_Child2
  //   │   └── Level1_Child2
  //   └── Level0_Child2
  auto root = CreateNode("Root");
  auto current_parent = root;

  // Create 4 levels of children (2 children per level)
  for (int level = 0; level < 4; ++level) {
    auto child1_opt
      = CreateChildNode(current_parent, fmt::format("Level{}_Child1", level));
    auto child2_opt
      = CreateChildNode(current_parent, fmt::format("Level{}_Child2", level));
    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());

    if (level < 3) { // Only set the next parent for first 3 levels
      current_parent = child1_opt.value();
    }
  }
  const auto initial_count = scene_->GetNodeCount();
  EXPECT_EQ(
    initial_count, 9); // Should have created 9 nodes (root + 4 levels with 2
                       // children each, but only one branch deepens)

  // Act: Destroy the entire hierarchy
  const bool destroyed = DestroyNodeHierarchy(root);
  // Assert: All nodes should be destroyed
  EXPECT_TRUE(destroyed);
  CHECK_FOR_FAILURES_MSG(ExpectSceneEmpty(),
    "expecting scene to be empty after large hierarchy destruction");
}

} // namespace
