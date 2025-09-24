//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "./SceneTest.h"

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::detail::TransformComponent;

namespace {

//=============================================================================
// Scene Reparenting Tests - MakeNodeRoot Functionality
//=============================================================================

class SceneReparentTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    scene_ = std::make_shared<Scene>("ReparentTestScene", 1024);
  }

  auto TearDown() -> void override { scene_.reset(); }

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

  [[nodiscard]] auto CreateChildNode(SceneNode& parent,
    const std::string& name) const -> std::optional<SceneNode>
  {
    return scene_->CreateChildNode(parent, name);
  }

  // -----------------------------------------------------------------------------
  // Common Scene Setups - Ready-to-use hierarchies
  // -----------------------------------------------------------------------------

  // Pattern: Parent -> Child
  struct SimpleParentChild {
    SceneNode parent;
    SceneNode child;
  };

  [[nodiscard]] auto CreateSimpleParentChild() const -> SimpleParentChild
  {
    auto parent = CreateNode("Parent");
    auto child_opt = CreateChildNode(parent, "Child");
    EXPECT_TRUE(child_opt.has_value());
    return { parent, *child_opt };
  }

  // Pattern: Parent -> Child1, Child2
  struct ParentWithTwoChildren {
    SceneNode parent;
    SceneNode child1;
    SceneNode child2;
  };

  [[nodiscard]] auto CreateParentWithTwoChildren() const
    -> ParentWithTwoChildren
  {
    auto parent = CreateNode("Parent");
    auto child1_opt = CreateChildNode(parent, "Child1");
    auto child2_opt = CreateChildNode(parent, "Child2");
    EXPECT_TRUE(child1_opt.has_value() && child2_opt.has_value());
    return { parent, *child1_opt, *child2_opt };
  }

  // Pattern: Root -> Child -> Grandchild (3 generations)
  struct ThreeGenerationHierarchy {
    SceneNode root;
    SceneNode child;
    SceneNode grandchild;
  };

  [[nodiscard]] auto CreateThreeGenerationHierarchy() const
    -> ThreeGenerationHierarchy
  {
    auto root = CreateNode("Root");
    auto child_opt = CreateChildNode(root, "Child");
    EXPECT_TRUE(child_opt.has_value());
    auto child = *child_opt;
    auto grandchild_opt = CreateChildNode(child, "Grandchild");
    EXPECT_TRUE(grandchild_opt.has_value());
    return { root, child, *grandchild_opt };
  }

  // Pattern: Root -> ParentA, ParentB (dual parent structure)
  struct DualParentStructure {
    SceneNode root;
    SceneNode parentA;
    SceneNode parentB;
  };

  [[nodiscard]] auto CreateDualParentStructure() const -> DualParentStructure
  {
    auto root = CreateNode("Root");
    auto parentA_opt = CreateChildNode(root, "ParentA");
    auto parentB_opt = CreateChildNode(root, "ParentB");
    EXPECT_TRUE(parentA_opt.has_value() && parentB_opt.has_value());
    return { root, *parentA_opt, *parentB_opt };
  }

  // Pattern: Root -> ParentA -> Child, Root -> ParentB (with child under
  // ParentA)
  struct DualParentWithChild {
    SceneNode root;
    SceneNode parentA;
    SceneNode parentB;
    SceneNode child;
  };

  [[nodiscard]] auto CreateDualParentWithChild() const -> DualParentWithChild
  {
    auto dual = CreateDualParentStructure();
    auto child_opt = CreateChildNode(dual.parentA, "Child");
    EXPECT_TRUE(child_opt.has_value());
    return { dual.root, dual.parentA, dual.parentB, *child_opt };
  }

  // Pattern: NodeA -> NodeB -> NodeC -> NodeD -> NodeE (linear chain)
  struct LinearChain {
    std::vector<SceneNode> nodes;
  };

  [[nodiscard]] auto CreateLinearChain(const int depth = 5) const -> LinearChain
  {
    std::vector<SceneNode> nodes;
    auto current = CreateNode("NodeA");
    nodes.push_back(current);

    const std::vector<std::string> names = { "NodeB", "NodeC", "NodeD", "NodeE",
      "NodeF", "NodeG", "NodeH", "NodeI", "NodeJ" };
    for (int i = 1; i < depth && i < static_cast<int>(names.size()) + 1; ++i) {
      auto child_opt = CreateChildNode(current, names[i - 1]);
      EXPECT_TRUE(child_opt.has_value());
      current = *child_opt;
      nodes.push_back(current);
    }

    return { std::move(nodes) };
  }

  // Helper: Set up transform with specific values and update as needed
  auto SetupNodeTransform(SceneNode& node,
    const TransformComponent::Vec3& position,
    const TransformComponent::Quat& rotation,
    const TransformComponent::Vec3& scale) const -> void
  {
    auto node_impl_opt = node.GetImpl();
    ASSERT_TRUE(node_impl_opt.has_value());

    auto& transform = node_impl_opt->get().GetComponent<TransformComponent>();
    transform.SetLocalTransform(position, rotation, scale);
  }

  // Helper: Get transform component from node
  auto GetTransformComponent(SceneNode& node) const -> TransformComponent&
  {
    auto node_impl_opt = node.GetImpl();
    EXPECT_TRUE(node_impl_opt.has_value())
      << "Node should have valid implementation";
    return node_impl_opt->get().GetComponent<TransformComponent>();
  }

  // Helper: Update scene transforms to ensure cached world values are valid
  auto UpdateSceneTransforms() const -> void
  {
    scene_->Update(false); // Update transforms without skipping dirty flags
  }

  // Helper: Create test vectors and quaternions
  static constexpr auto MakeVec3(const float x, const float y, const float z)
    -> TransformComponent::Vec3
  {
    return TransformComponent::Vec3 { x, y, z };
  }

  static auto MakeQuat(const float w, const float x, const float y,
    const float z) -> TransformComponent::Quat
  {
    return TransformComponent::Quat { w, x, y, z };
  }

  static auto QuatFromEuler(const float x_deg, const float y_deg,
    const float z_deg) -> TransformComponent::Quat
  {
    return TransformComponent::Quat { glm::radians(
      TransformComponent::Vec3 { x_deg, y_deg, z_deg }) };
  }
  // Helper: Verify node is valid and has expected name
  static auto ExpectNodeWithName(SceneNode& node, const std::string& name)
    -> void
  {
    ASSERT_TRUE(node.IsValid()) << "Node should be valid";
    const auto obj_opt = node.GetImpl();
    ASSERT_TRUE(obj_opt.has_value()) << "Node object should be present";
    EXPECT_EQ(obj_opt->get().GetName(), name)
      << "Node name mismatch: expected '" << name << "', got '"
      << obj_opt->get().GetName();
  }
  // Helper: Verify node is valid, has expected name, and is a root node
  static auto ExpectNodeValidAsRoot(SceneNode& node, const std::string& name)
    -> void
  {
    ExpectNodeWithName(node, name);
    EXPECT_TRUE(node.IsRoot()) << "Node '" << name << "' should be a root node";
    EXPECT_FALSE(node.HasParent())
      << "Root node '" << name << "' should not have a parent";
  }
  // Helper: Verify node is valid, has expected parent, and is not a root
  static auto ExpectNodeValidWithParent(
    SceneNode& node, const SceneNode& expected_parent) -> void
  {
    ASSERT_TRUE(node.IsValid()) << "Node should be valid";
    ASSERT_TRUE(expected_parent.IsValid()) << "Expected parent should be valid";
    EXPECT_FALSE(node.IsRoot())
      << "Node should not be a root (should have parent)";
    EXPECT_TRUE(node.HasParent()) << "Node should have a parent";

    const auto parent_opt = node.GetParent();
    ASSERT_TRUE(parent_opt.has_value()) << "Node should have a valid parent";

    EXPECT_EQ(parent_opt->GetHandle(), expected_parent.GetHandle())
      << "Node has wrong parent (handle mismatch)";
  }

  // Helper: Verify vectors are approximately equal
  static auto ExpectVec3Near(const TransformComponent::Vec3& actual,
    const TransformComponent::Vec3& expected, const float tolerance = 1e-5f)
    -> void
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // Helper: Verify quaternions are approximately equal
  static auto ExpectQuatNear(const TransformComponent::Quat& actual,
    const TransformComponent::Quat& expected, const float tolerance = 1e-5f)
    -> void
  {
    EXPECT_NEAR(actual.w, expected.w, tolerance);
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  std::shared_ptr<Scene> scene_;
};

// -----------------------------------------------------------------------------
// Normal Operation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_ValidChildNode_BecomesRoot)
{
  // Arrange: Create parent-child hierarchy
  auto hierarchy = CreateSimpleParentChild();
  EXPECT_FALSE(hierarchy.child.IsRoot());
  EXPECT_TRUE(hierarchy.child.HasParent());

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(hierarchy.child, false);

  // Assert: Operation should succeed and child becomes root
  EXPECT_TRUE(result);
  EXPECT_TRUE(hierarchy.child.IsRoot());
  EXPECT_FALSE(hierarchy.child.HasParent());
  GCHECK_F(ExpectNodeWithName(hierarchy.child, "Child"));
  GCHECK_F(ExpectNodeWithName(hierarchy.parent, "Parent"));
}

NOLINT_TEST_F(
  SceneReparentTest, MakeNodeRoot_AlreadyRootNode_SucceedsImmediately)
{
  // Arrange: Create root node
  auto root = CreateNode("RootNode");
  EXPECT_TRUE(root.IsRoot());

  // Act: Try to make already-root node a root
  const auto result = scene_->MakeNodeRoot(root, false);
  // Assert: Operation should succeed with no changes
  EXPECT_TRUE(result);
  EXPECT_TRUE(root.IsRoot());
  GCHECK_F(ExpectNodeWithName(root, "RootNode"));
}

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_DeepHierarchy_EntireSubtreeMoved)
{
  // Arrange: Create deep hierarchy: Root -> A -> B -> C
  auto root = CreateNode("Root");
  auto nodeA_opt = CreateChildNode(root, "NodeA");
  ASSERT_TRUE(nodeA_opt.has_value());
  auto nodeA = *nodeA_opt;

  auto nodeB_opt = CreateChildNode(nodeA, "NodeB");
  ASSERT_TRUE(nodeB_opt.has_value());
  auto nodeB = *nodeB_opt;

  auto nodeC_opt = CreateChildNode(nodeB, "NodeC");
  ASSERT_TRUE(nodeC_opt.has_value());
  auto nodeC = *nodeC_opt;

  // Act: Make NodeA a root (moving A -> B -> C subtree)
  const auto result = scene_->MakeNodeRoot(nodeA, false);

  // Assert: Entire subtree should be moved to top level
  EXPECT_TRUE(result);
  EXPECT_TRUE(nodeA.IsRoot());
  EXPECT_FALSE(nodeA.HasParent());
  EXPECT_TRUE(nodeA.HasChildren());

  // Assert: Internal hierarchy preserved
  EXPECT_FALSE(nodeB.IsRoot());
  EXPECT_TRUE(nodeB.HasParent());
  EXPECT_FALSE(nodeC.IsRoot());
  EXPECT_TRUE(nodeC.HasParent());
}

NOLINT_TEST_F(SceneReparentTest,
  MakeNodeRoot_WithoutTransformPreservation_MarksSubtreeDirty)
{
  // Arrange: Create parent-child with transforms
  auto hierarchy = CreateSimpleParentChild();

  SetupNodeTransform(hierarchy.parent, MakeVec3(10, 20, 30),
    QuatFromEuler(45, 0, 0), MakeVec3(2, 2, 2));
  SetupNodeTransform(hierarchy.child, MakeVec3(1, 2, 3),
    QuatFromEuler(0, 45, 0), MakeVec3(1, 1, 1));
  UpdateSceneTransforms();

  // Act: Make child root without preserving transform
  const auto result = scene_->MakeNodeRoot(hierarchy.child, false);

  // Assert: Transform should be marked dirty for recalculation
  EXPECT_TRUE(result);
  auto& child_transform = GetTransformComponent(hierarchy.child);
  EXPECT_TRUE(child_transform.IsDirty());
}

NOLINT_TEST_F(SceneReparentTest,
  MakeNodeRoot_WithTransformPreservation_PreservesWorldPosition)
{
  // Arrange: Create parent-child with transforms
  auto hierarchy = CreateSimpleParentChild();

  // Set parent transform: position(10,20,30), rotation(45° around X),
  // scale(2,2,2)
  SetupNodeTransform(hierarchy.parent, MakeVec3(10, 20, 30),
    QuatFromEuler(45, 0, 0), MakeVec3(2, 2, 2));
  // Set child transform: position(1,2,3), rotation(45° around Y), scale(1,1,1)
  SetupNodeTransform(hierarchy.child, MakeVec3(1, 2, 3),
    QuatFromEuler(0, 45, 0), MakeVec3(1, 1, 1));

  UpdateSceneTransforms(); // Update cached world transforms

  // Capture world transform before reparenting
  auto& child_transform = GetTransformComponent(hierarchy.child);
  const auto original_world_pos = child_transform.GetWorldPosition();
  const auto original_world_rot = child_transform.GetWorldRotation();
  const auto original_world_scale = child_transform.GetWorldScale();

  // Act: Make child root with transform preservation
  const auto result = scene_->MakeNodeRoot(hierarchy.child, true);

  // Assert: Operation succeeds and world transform is preserved
  EXPECT_TRUE(result);
  EXPECT_TRUE(hierarchy.child.IsRoot());

  // Assert: Local transform should now equal the captured world transform
  GCHECK_F(
    ExpectVec3Near(child_transform.GetLocalPosition(), original_world_pos));
  GCHECK_F(
    ExpectQuatNear(child_transform.GetLocalRotation(), original_world_rot));
  GCHECK_F(
    ExpectVec3Near(child_transform.GetLocalScale(), original_world_scale));
}

NOLINT_TEST_F(SceneReparentTest, MakeNodesRoot_ValidNodes_AllSucceed)
{
  // Arrange: Create multiple parent-child hierarchies
  auto hierarchy1 = CreateSimpleParentChild();
  auto hierarchy2 = CreateParentWithTwoChildren();

  // Create a standalone root that's already root
  auto standalone_root = CreateNode("StandaloneRoot");

  // Collect nodes to make root
  std::vector<SceneNode> nodes_to_root = {
    hierarchy1.child, // child node
    hierarchy2.child1, // another child node
    hierarchy2.child2, // yet another child node
    standalone_root // already root node
  };

  // Verify initial state
  EXPECT_FALSE(hierarchy1.child.IsRoot());
  EXPECT_FALSE(hierarchy2.child1.IsRoot());
  EXPECT_FALSE(hierarchy2.child2.IsRoot());
  EXPECT_TRUE(standalone_root.IsRoot());

  const auto initial_root_count = scene_->GetRootNodes().size();

  // Act: Make all nodes root
  const auto results = scene_->MakeNodesRoot(nodes_to_root, false);

  // Assert: All operations should succeed
  ASSERT_EQ(results.size(), nodes_to_root.size());
  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i], 1) << "Operation " << i << " should succeed";
  }

  // Assert: All nodes should now be root
  EXPECT_TRUE(hierarchy1.child.IsRoot());
  EXPECT_TRUE(hierarchy2.child1.IsRoot());
  EXPECT_TRUE(hierarchy2.child2.IsRoot());
  EXPECT_TRUE(standalone_root.IsRoot());

  // Assert: Root count should increase by 3 (standalone_root was already root)
  EXPECT_EQ(scene_->GetRootNodes().size(), initial_root_count + 3);

  // Assert: Original parents should lose their children
  EXPECT_FALSE(hierarchy1.parent.HasChildren());
  EXPECT_FALSE(hierarchy2.parent.HasChildren());
}

// -----------------------------------------------------------------------------
// Scene State Consistency Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_UpdatesRootNodesList)
{
  // Arrange: Create parent-child hierarchy
  auto hierarchy = CreateSimpleParentChild();

  const auto initial_root_count = scene_->GetRootNodes().size();

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(hierarchy.child, false);

  // Assert: Root nodes list should be updated
  EXPECT_TRUE(result);
  const auto final_root_count = scene_->GetRootNodes().size();
  EXPECT_EQ(final_root_count, initial_root_count + 1);

  // Assert: Child should be findable in root nodes
  auto root_nodes = scene_->GetRootNodes();
  const auto found_child
    = std::ranges::find_if(root_nodes, [&](SceneNode& node) {
        const auto obj_opt = node.GetImpl();
        return obj_opt.has_value() && obj_opt->get().GetName() == "Child";
      });
  EXPECT_NE(found_child, root_nodes.end());
}

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_PreservesSceneNodeCount)
{
  // Arrange: Create parent-child hierarchy
  auto hierarchy = CreateSimpleParentChild();

  const auto initial_node_count = scene_->GetNodeCount();

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(hierarchy.child, false);

  // Assert: Total node count should remain the same
  EXPECT_TRUE(result);
  EXPECT_EQ(scene_->GetNodeCount(), initial_node_count);
}

// -----------------------------------------------------------------------------
// Error Handling Tests
// -----------------------------------------------------------------------------

class SceneReparentErrorTest : public SceneReparentTest { };

NOLINT_TEST_F(SceneReparentErrorTest, MakeNodeRoot_InvalidNode_ReturnsFalse)
{
  // Arrange: Create invalid node
  SceneNode invalid_node;
  EXPECT_FALSE(invalid_node.IsValid());

  // Act: Try to make invalid node root
  const auto result = scene_->MakeNodeRoot(invalid_node, false);

  // Assert: Operation should fail
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(
  SceneReparentErrorTest, MakeNodeRoot_LazilyInvalidatedNode_ReturnsFalse)
{
  // Arrange: Create node then destroy it to trigger lazy invalidation
  auto node = CreateNode("TestNode");
  EXPECT_TRUE(node.IsValid());

  // Destroy the node, making handles invalid
  scene_->DestroyNodeHierarchy(node);

  // Act: Try to make destroyed node root
  const auto result = scene_->MakeNodeRoot(node, false);

  // Assert: Operation should fail and node should be invalidated
  EXPECT_FALSE(result);
  EXPECT_FALSE(node.IsValid());
}

NOLINT_TEST_F(
  SceneReparentErrorTest, MakeNodesRoot_EmptySpan_ReturnsEmptyVector)
{
  // Arrange: Empty span of nodes
  std::vector<SceneNode> empty_nodes;

  // Act: Try to make empty span root
  const auto results = scene_->MakeNodesRoot(empty_nodes, false);

  // Assert: Should return empty vector
  EXPECT_TRUE(results.empty());
}

// -----------------------------------------------------------------------------
// Death Tests
// -----------------------------------------------------------------------------

class SceneReparentDeathTest : public SceneReparentTest { };

NOLINT_TEST_F(SceneReparentDeathTest, MakeNodeRoot_NodeFromDifferentScene_Dies)
{
  // Arrange: Create node from different scene
  auto other_scene = std::make_shared<Scene>("OtherScene", 64);
  auto foreign_node = other_scene->CreateNode("ForeignNode");
  EXPECT_TRUE(foreign_node.IsValid());

  // Act & Assert: Should terminate program
  EXPECT_DEATH(
    { [[maybe_unused]] auto _ = scene_->MakeNodeRoot(foreign_node, false); },
    ".*"); // Death message will contain scene ownership check failure
}

NOLINT_TEST_F(SceneReparentDeathTest, ReparentNode_NodeFromDifferentScene_Dies)
{
  // Arrange: Create node from different scene
  auto other_scene = std::make_shared<Scene>("OtherScene", 64);
  auto foreign_node = other_scene->CreateNode("ForeignNode");
  auto local_parent = CreateNode("LocalParent");

  EXPECT_TRUE(foreign_node.IsValid());
  EXPECT_TRUE(local_parent.IsValid());

  // Act & Assert: Should terminate program
  EXPECT_DEATH(
    {
      [[maybe_unused]] auto _
        = scene_->ReparentNode(foreign_node, local_parent, false);
    },
    ".*"); // Death message will contain scene ownership check failure
}

NOLINT_TEST_F(
  SceneReparentDeathTest, ReparentNode_ParentFromDifferentScene_Dies)
{
  // Arrange: Create parent from different scene
  auto other_scene = std::make_shared<Scene>("OtherScene", 64);
  auto foreign_parent = other_scene->CreateNode("ForeignParent");
  auto local_node = CreateNode("LocalNode");

  EXPECT_TRUE(foreign_parent.IsValid());
  EXPECT_TRUE(local_node.IsValid());

  // Act & Assert: Should terminate program
  EXPECT_DEATH(
    {
      [[maybe_unused]] auto _
        = scene_->ReparentNode(local_node, foreign_parent, false);
    },
    ".*"); // Death message will contain scene ownership check failure
}

// -----------------------------------------------------------------------------
// Edge Cases and Weird Scenarios Tests
// -----------------------------------------------------------------------------

class SceneReparentEdgeTest : public SceneReparentTest { };

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_NodeWithManyChildren_PreservesAllChildren)
{
  // Arrange: Create node with many children
  auto parent = CreateNode("Parent");
  std::vector<SceneNode> children;

  for (int i = 0; i < 10; ++i) {
    auto child_opt = CreateChildNode(parent, "Child" + std::to_string(i));
    ASSERT_TRUE(child_opt.has_value());
    children.push_back(*child_opt);
  }

  // Act: Make parent a root (it already is, but test the path)
  const auto result = scene_->MakeNodeRoot(parent, false);

  // Assert: All children should still be children of parent
  EXPECT_TRUE(result);
  EXPECT_TRUE(parent.HasChildren());

  for (auto& child : children) {
    EXPECT_FALSE(child.IsRoot());
    EXPECT_TRUE(child.HasParent());
    EXPECT_TRUE(child.IsValid());
  }
}

NOLINT_TEST_F(SceneReparentEdgeTest, MakeNodeRoot_EmptyNameNode_WorksCorrectly)
{
  // Arrange: Create nodes with empty and unusual names (one-off test)
  auto parent = CreateNode(""); // Empty name
  auto child_opt = CreateChildNode(parent, "   "); // Whitespace name
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  // Act: Make child with whitespace name a root
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Should work despite unusual names
  EXPECT_TRUE(result);
  EXPECT_TRUE(child.IsRoot());
  GCHECK_F(ExpectNodeWithName(child, "   "));
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_VeryDeepHierarchy_HandledCorrectly)
{
  // Arrange: Create very deep hierarchy (15 levels)
  auto deep_chain = CreateLinearChain(15);

  // Act: Make deep child a root (moving 14-level subtree)
  auto middle_node = deep_chain.nodes[7]; // Node at level 7
  const auto result = scene_->MakeNodeRoot(middle_node, false);

  // Assert: Should handle deep hierarchy correctly
  EXPECT_TRUE(result);
  EXPECT_TRUE(middle_node.IsRoot());

  // Verify subtree structure is preserved
  for (size_t i = 8; i < deep_chain.nodes.size(); ++i) {
    EXPECT_FALSE(deep_chain.nodes[i].IsRoot());
    EXPECT_TRUE(deep_chain.nodes[i].IsValid());
  }
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_ImmediatelyAfterCreation_WorksCorrectly)
{
  // Arrange: Create child and immediately make it root
  auto hierarchy = CreateSimpleParentChild();

  // Act: Make root immediately without any intervening operations
  const auto result = scene_->MakeNodeRoot(hierarchy.child, true);

  // Assert: Should work correctly even with minimal setup
  EXPECT_TRUE(result);
  EXPECT_TRUE(hierarchy.child.IsRoot());
  EXPECT_TRUE(hierarchy.child.IsValid());
}

NOLINT_TEST_F(SceneReparentEdgeTest,
  MakeNodeRoot_WithTransformPreservation_ZeroScaleHandling)
{
  // Arrange: Create child with zero scale on one axis
  auto hierarchy = CreateSimpleParentChild();

  SetupNodeTransform(hierarchy.parent, MakeVec3(10, 10, 10),
    QuatFromEuler(0, 0, 0), MakeVec3(1, 1, 1));
  SetupNodeTransform(hierarchy.child, MakeVec3(0, 0, 0), QuatFromEuler(0, 0, 0),
    MakeVec3(0, 1, 1)); // Zero X scale
  UpdateSceneTransforms();

  // Act: Should handle zero scale gracefully
  EXPECT_NO_FATAL_FAILURE({
    const auto result = scene_->MakeNodeRoot(hierarchy.child, true);
    EXPECT_TRUE(result);
  });
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_MultipleConcurrentOperations_AllSucceed)
{
  // Arrange: Create multiple sibling nodes
  auto parent = CreateNode("Parent");
  std::vector<SceneNode> children;

  for (int i = 0; i < 5; ++i) {
    auto child_opt = CreateChildNode(parent, "Child" + std::to_string(i));
    ASSERT_TRUE(child_opt.has_value());
    children.push_back(*child_opt);
  }

  // Act: Make all children roots in sequence
  std::vector<bool> results;
  for (auto& child : children) {
    results.push_back(scene_->MakeNodeRoot(child, false));
  }

  // Assert: All operations should succeed
  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_TRUE(results[i]) << "Operation " << i << " should succeed";
    EXPECT_TRUE(children[i].IsRoot()) << "Child " << i << " should be root";
    EXPECT_TRUE(children[i].IsValid()) << "Child " << i << " should be valid";
  }
}

// -----------------------------------------------------------------------------
// ReparentNode Normal Operation Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneReparentTest, ReparentNode_ValidNodes_SucceedsAndMovesHierarchy)
{
  // Arrange: Create dual parent structure with child under ParentA
  auto hierarchy = CreateDualParentWithChild();

  // Verify initial setup
  GCHECK_F(ExpectNodeValidWithParent(hierarchy.child, hierarchy.parentA));
  EXPECT_TRUE(hierarchy.parentA.HasChildren());
  EXPECT_FALSE(hierarchy.parentB.HasChildren());

  // Act: Reparent child from ParentA to ParentB
  const auto result
    = scene_->ReparentNode(hierarchy.child, hierarchy.parentB, false);

  // Assert: Child should now be under ParentB
  EXPECT_TRUE(result);
  GCHECK_F(ExpectNodeValidWithParent(hierarchy.child, hierarchy.parentB));
  EXPECT_FALSE(hierarchy.parentA.HasChildren());
  EXPECT_TRUE(hierarchy.parentB.HasChildren());
}

NOLINT_TEST_F(
  SceneReparentTest, ReparentNode_RootToParent_SucceedsAndUpdatesRootList)
{
  // Arrange: Create root node and a parent
  auto standalone_root = CreateNode("StandaloneRoot");
  auto parent = CreateNode("Parent");
  GCHECK_F(ExpectNodeValidAsRoot(standalone_root, "StandaloneRoot"));
  GCHECK_F(ExpectNodeValidAsRoot(parent, "Parent"));

  const auto initial_root_count = scene_->GetRootNodes().size();

  // Act: Reparent standalone root to become child of parent
  const auto result = scene_->ReparentNode(standalone_root, parent, false);

  // Assert: standalone_root should no longer be a root
  EXPECT_TRUE(result);
  GCHECK_F(ExpectNodeValidWithParent(standalone_root, parent));

  // Root count should decrease by 1
  EXPECT_EQ(scene_->GetRootNodes().size(), initial_root_count - 1);
}

NOLINT_TEST_F(
  SceneReparentTest, ReparentNode_WithEntireSubtree_PreservesInternalStructure)
{
  // Arrange: Create hierarchy with subtree: Root -> ParentA -> Child ->
  // Grandchild, Root -> ParentB
  auto dual = CreateDualParentStructure();
  auto child_opt = CreateChildNode(dual.parentA, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  auto grandchild_opt = CreateChildNode(child, "Grandchild");
  ASSERT_TRUE(grandchild_opt.has_value());
  auto grandchild = *grandchild_opt;

  // Act: Reparent entire child subtree from ParentA to ParentB
  const auto result = scene_->ReparentNode(child, dual.parentB, false);

  // Assert: Entire subtree moved, internal structure preserved
  EXPECT_TRUE(result);
  GCHECK_F(ExpectNodeValidWithParent(child, dual.parentB));
  GCHECK_F(ExpectNodeValidWithParent(grandchild, child));
  EXPECT_TRUE(child.HasChildren());
  EXPECT_FALSE(dual.parentA.HasChildren());
  EXPECT_TRUE(dual.parentB.HasChildren());
}

NOLINT_TEST_F(SceneReparentTest,
  ReparentNode_WithTransformPreservation_MaintainsWorldTransform)
{
  // Arrange: Create hierarchy with transforms using DualParentWithChild
  auto hierarchy = CreateDualParentWithChild();

  // Set up transforms
  SetupNodeTransform(hierarchy.parentA, MakeVec3(10, 0, 0),
    QuatFromEuler(0, 0, 0), MakeVec3(2, 2, 2));
  SetupNodeTransform(hierarchy.parentB, MakeVec3(0, 10, 0),
    QuatFromEuler(0, 90, 0), MakeVec3(1, 1, 1));
  SetupNodeTransform(hierarchy.child, MakeVec3(5, 0, 0), QuatFromEuler(0, 0, 0),
    MakeVec3(1, 1, 1));
  UpdateSceneTransforms();

  // Capture world transform before reparenting
  auto& child_transform = GetTransformComponent(hierarchy.child);
  const auto original_world_pos = child_transform.GetWorldPosition();
  const auto original_world_rot = child_transform.GetWorldRotation();
  const auto original_world_scale = child_transform.GetWorldScale();

  // Act: Reparent with transform preservation
  const auto result
    = scene_->ReparentNode(hierarchy.child, hierarchy.parentB, true);

  // Assert: World transform should be preserved
  EXPECT_TRUE(result);
  UpdateSceneTransforms(); // Update to get new world transforms

  GCHECK_F(
    ExpectVec3Near(child_transform.GetWorldPosition(), original_world_pos));
  GCHECK_F(
    ExpectQuatNear(child_transform.GetWorldRotation(), original_world_rot));
  GCHECK_F(
    ExpectVec3Near(child_transform.GetWorldScale(), original_world_scale));
}

// -----------------------------------------------------------------------------
// Cycle Detection Tests (Edge Cases)
// -----------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneReparentEdgeTest, ReparentNode_SelfAsParent_DetectsCycleAndFails)
{
  // Arrange: Create a simple node
  auto node = CreateNode("SelfParentNode");

  // Act: Try to make node its own parent
  const auto result = scene_->ReparentNode(node, node, false);
  // Assert: Should detect cycle and fail
  EXPECT_FALSE(result);
  GCHECK_F(ExpectNodeValidAsRoot(node, "SelfParentNode"));
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, ReparentNode_DirectChildAsParent_DetectsCycleAndFails)
{
  // Arrange: Create Parent -> Child hierarchy
  auto hierarchy = CreateSimpleParentChild();

  // Act: Try to make parent a child of its own child (direct cycle)
  const auto result
    = scene_->ReparentNode(hierarchy.parent, hierarchy.child, false);

  // Assert: Should detect cycle and fail
  EXPECT_FALSE(result);
  GCHECK_F(ExpectNodeValidAsRoot(hierarchy.parent, "Parent"));
  GCHECK_F(ExpectNodeValidWithParent(hierarchy.child, hierarchy.parent));
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, ReparentNode_GrandchildAsParent_DetectsCycleAndFails)
{
  // Arrange: Create A -> B -> C hierarchy
  const auto chain = CreateLinearChain(3);
  auto nodeA = chain.nodes[0]; // "NodeA"
  auto nodeB = chain.nodes[1]; // "NodeB"
  auto nodeC = chain.nodes[2]; // "NodeC"

  // Act: Try to make A a child of C (would create cycle: C -> A -> B -> C)
  const auto result = scene_->ReparentNode(nodeA, nodeC, false);

  // Assert: Should detect cycle and fail
  EXPECT_FALSE(result);
  GCHECK_F(ExpectNodeValidAsRoot(nodeA, "NodeA"));
  GCHECK_F(ExpectNodeValidWithParent(nodeB, nodeA));
  GCHECK_F(ExpectNodeValidWithParent(nodeC, nodeB));
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, ReparentNode_DeepHierarchyCycle_DetectsCycleAndFails)
{
  // Arrange: Create deep hierarchy: A -> B -> C -> D -> E
  const auto chain = CreateLinearChain(5);
  auto nodeA = chain.nodes[0]; // "NodeA"
  auto nodeB = chain.nodes[1]; // "NodeB"
  auto nodeE = chain.nodes[4]; // "NodeE"

  // Act: Try to make B a child of E (would create cycle through deep hierarchy)
  const auto result = scene_->ReparentNode(nodeB, nodeE, false);

  // Assert: Should detect cycle and fail, hierarchy unchanged
  EXPECT_FALSE(result);
  GCHECK_F(ExpectNodeValidAsRoot(nodeA, "NodeA"));
  GCHECK_F(ExpectNodeValidWithParent(nodeB, nodeA));
  // NodeE should still be a descendant of NodeA through the chain
  EXPECT_FALSE(nodeE.IsRoot());
}

NOLINT_TEST_F(SceneReparentEdgeTest,
  ReparentNode_ValidReparentingAfterCycleDetection_Succeeds)
{
  // Arrange: Create A -> B -> C hierarchy and separate D
  auto nodeA = CreateNode("NodeA");
  auto nodeB_opt = CreateChildNode(nodeA, "NodeB");
  ASSERT_TRUE(nodeB_opt.has_value());
  auto nodeB = *nodeB_opt;

  auto nodeC_opt = CreateChildNode(nodeB, "NodeC");
  ASSERT_TRUE(nodeC_opt.has_value());
  auto nodeC = *nodeC_opt;

  auto nodeD = CreateNode("NodeD");

  // Act: First try invalid operation (would create cycle)
  const auto invalid_result = scene_->ReparentNode(nodeA, nodeC, false);
  EXPECT_FALSE(invalid_result);

  // Act: Then try valid operation (no cycle)
  const auto valid_result = scene_->ReparentNode(nodeC, nodeD, false);
  // Assert: Valid operation should succeed
  EXPECT_TRUE(valid_result);
  GCHECK_F(ExpectNodeValidWithParent(nodeC, nodeD));
  EXPECT_FALSE(nodeB.HasChildren()); // B no longer has C as child
  EXPECT_TRUE(nodeD.HasChildren()); // D now has C as child
}

// -----------------------------------------------------------------------------
// ReparentNode Error Handling Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneReparentErrorTest, ReparentNode_InvalidNode_ReturnsFalse)
{
  // Arrange: Create valid parent and invalid node
  auto parent = CreateNode("ValidParent");
  SceneNode invalid_node;
  EXPECT_FALSE(invalid_node.IsValid());

  // Act: Try to reparent invalid node
  const auto result = scene_->ReparentNode(invalid_node, parent, false);

  // Assert: Should fail gracefully
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(SceneReparentErrorTest, ReparentNode_InvalidParent_ReturnsFalse)
{
  // Arrange: Create valid node and invalid parent
  auto node = CreateNode("ValidNode");
  SceneNode invalid_parent;
  EXPECT_FALSE(invalid_parent.IsValid());

  // Act: Try to reparent to invalid parent
  const auto result = scene_->ReparentNode(node, invalid_parent, false);

  // Assert: Should fail gracefully
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(
  SceneReparentErrorTest, ReparentNode_LazilyInvalidatedNode_ReturnsFalse)
{
  // Arrange: Create nodes then destroy one to trigger lazy invalidation
  auto parent = CreateNode("Parent");
  auto node = CreateNode("NodeToDestroy");

  // Destroy the node, making handles invalid
  scene_->DestroyNodeHierarchy(node);

  // Act: Try to reparent destroyed node
  const auto result = scene_->ReparentNode(node, parent, false);

  // Assert: Should fail and node should be invalidated
  EXPECT_FALSE(result);
  EXPECT_FALSE(node.IsValid());
}

NOLINT_TEST_F(
  SceneReparentErrorTest, MakeNodesRoot_MixedValidInvalid_ReportsPartialFailure)
{
  // Arrange: Create mix of valid and invalid nodes
  auto hierarchy = CreateSimpleParentChild();
  auto invalid_node = CreateNodeWithInvalidHandle();
  auto lazy_invalid_node = CreateLazyInvalidationNode("LazyInvalid");

  std::vector<SceneNode> mixed_nodes = {
    hierarchy.child, // valid child node
    invalid_node, // invalid node handle
    hierarchy.parent, // valid root node (already root)
    lazy_invalid_node // lazily invalidated node
  };

  // Verify initial state
  EXPECT_TRUE(hierarchy.child.IsValid());
  EXPECT_FALSE(invalid_node.IsValid());
  EXPECT_TRUE(hierarchy.parent.IsValid());
  EXPECT_TRUE(
    lazy_invalid_node.IsValid()); // Still appears valid until accessed

  // Act: Try to make mixed nodes root
  const auto results = scene_->MakeNodesRoot(mixed_nodes, false);

  // Assert: Results vector should match input size
  ASSERT_EQ(results.size(), mixed_nodes.size());

  // Assert: Expected success/failure pattern
  EXPECT_EQ(results[0], 1) << "Valid child should succeed";
  EXPECT_EQ(results[1], 0) << "Invalid node should fail";
  EXPECT_EQ(results[2], 1) << "Valid root should succeed";
  EXPECT_EQ(results[3], 0) << "Lazily invalid node should fail";

  // Assert: Only valid operations should have taken effect
  EXPECT_TRUE(hierarchy.child.IsRoot()) << "Valid child should now be root";
  EXPECT_TRUE(hierarchy.parent.IsRoot()) << "Parent should remain root";
}

} // namespace
