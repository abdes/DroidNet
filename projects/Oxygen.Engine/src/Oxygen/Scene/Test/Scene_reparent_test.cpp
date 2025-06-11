//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::detail::TransformComponent;

namespace {

//=============================================================================
// Scene Reparenting Tests - MakeNodeRoot Functionality
//=============================================================================

class SceneReparentTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("ReparentTestScene", 1024);
  }

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

  [[nodiscard]] auto CreateChildNode(const SceneNode& parent,
    const std::string& name) const -> std::optional<SceneNode>
  {
    return scene_->CreateChildNode(parent, name);
  }

  // Helper: Set up transform with specific values and update as needed
  void SetupNodeTransform(const SceneNode& node,
    const TransformComponent::Vec3& position,
    const TransformComponent::Quat& rotation,
    const TransformComponent::Vec3& scale) const
  {
    auto node_impl_opt = node.GetObject();
    ASSERT_TRUE(node_impl_opt.has_value());

    auto& transform = node_impl_opt->get().GetComponent<TransformComponent>();
    transform.SetLocalTransform(position, rotation, scale);
  }

  // Helper: Get transform component from node
  auto GetTransformComponent(const SceneNode& node) const -> TransformComponent&
  {
    auto node_impl_opt = node.GetObject();
    EXPECT_TRUE(node_impl_opt.has_value())
      << "Node should have valid implementation";
    return node_impl_opt->get().GetComponent<TransformComponent>();
  }

  // Helper: Update scene transforms to ensure cached world values are valid
  void UpdateSceneTransforms() const
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
  static void ExpectNodeValidWithName(
    const SceneNode& node, const std::string& name)
  {
    if (!node.IsValid())
      FAIL() << "Node should be valid";
    const auto obj_opt = node.GetObject();
    if (!obj_opt.has_value())
      FAIL() << "Node object should be present";
    if (obj_opt->get().GetName() != name)
      FAIL() << "Node name mismatch: expected '" << name << "', got '"
             << obj_opt->get().GetName() << "'";
  }

  // Helper: Verify vectors are approximately equal
  static void ExpectVec3Near(const TransformComponent::Vec3& actual,
    const TransformComponent::Vec3& expected, const float tolerance = 1e-5f)
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // Helper: Verify quaternions are approximately equal
  static void ExpectQuatNear(const TransformComponent::Quat& actual,
    const TransformComponent::Quat& expected, const float tolerance = 1e-5f)
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
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  EXPECT_FALSE(child.IsRoot());
  EXPECT_TRUE(child.HasParent());

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Operation should succeed and child becomes root
  EXPECT_TRUE(result);
  EXPECT_TRUE(child.IsRoot());
  EXPECT_FALSE(child.HasParent());
  CHECK_FOR_FAILURES_MSG(
    ExpectNodeValidWithName(child, "Child"), "child node after making root");
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(parent, "Parent"),
    "parent node after child made root");
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
  CHECK_FOR_FAILURES_MSG(ExpectNodeValidWithName(root, "RootNode"),
    "root node after redundant operation");
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
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  SetupNodeTransform(
    parent, MakeVec3(10, 20, 30), QuatFromEuler(45, 0, 0), MakeVec3(2, 2, 2));
  SetupNodeTransform(
    child, MakeVec3(1, 2, 3), QuatFromEuler(0, 45, 0), MakeVec3(1, 1, 1));
  UpdateSceneTransforms();

  // Act: Make child root without preserving transform
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Transform should be marked dirty for recalculation
  EXPECT_TRUE(result);
  auto& child_transform = GetTransformComponent(child);
  EXPECT_TRUE(child_transform.IsDirty());
}

NOLINT_TEST_F(SceneReparentTest,
  MakeNodeRoot_WithTransformPreservation_PreservesWorldPosition)
{
  // Arrange: Create parent-child with transforms
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  // Set parent transform: position(10,20,30), rotation(45° around X),
  // scale(2,2,2)
  SetupNodeTransform(
    parent, MakeVec3(10, 20, 30), QuatFromEuler(45, 0, 0), MakeVec3(2, 2, 2));
  // Set child transform: position(1,2,3), rotation(45° around Y), scale(1,1,1)
  SetupNodeTransform(
    child, MakeVec3(1, 2, 3), QuatFromEuler(0, 45, 0), MakeVec3(1, 1, 1));

  UpdateSceneTransforms(); // Update cached world transforms

  // Capture world transform before reparenting
  auto& child_transform = GetTransformComponent(child);
  const auto original_world_pos = child_transform.GetWorldPosition();
  const auto original_world_rot = child_transform.GetWorldRotation();
  const auto original_world_scale = child_transform.GetWorldScale();

  // Act: Make child root with transform preservation
  const auto result = scene_->MakeNodeRoot(child, true);

  // Assert: Operation succeeds and world transform is preserved
  EXPECT_TRUE(result);
  EXPECT_TRUE(child.IsRoot());

  // Assert: Local transform should now equal the captured world transform
  CHECK_FOR_FAILURES_MSG(
    ExpectVec3Near(child_transform.GetLocalPosition(), original_world_pos),
    "preserved world position as local");
  CHECK_FOR_FAILURES_MSG(
    ExpectQuatNear(child_transform.GetLocalRotation(), original_world_rot),
    "preserved world rotation as local");
  CHECK_FOR_FAILURES_MSG(
    ExpectVec3Near(child_transform.GetLocalScale(), original_world_scale),
    "preserved world scale as local");
}

// -----------------------------------------------------------------------------
// Scene State Consistency Tests
// -----------------------------------------------------------------------------

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_UpdatesRootNodesList)
{
  // Arrange: Create parent-child hierarchy
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  const auto initial_root_count = scene_->GetRootNodes().size();

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Root nodes list should be updated
  EXPECT_TRUE(result);
  const auto final_root_count = scene_->GetRootNodes().size();
  EXPECT_EQ(final_root_count, initial_root_count + 1);

  // Assert: Child should be findable in root nodes
  const auto root_nodes = scene_->GetRootNodes();
  const auto found_child = std::find_if(
    root_nodes.begin(), root_nodes.end(), [&](const SceneNode& node) {
      auto obj_opt = node.GetObject();
      return obj_opt.has_value() && obj_opt->get().GetName() == "Child";
    });
  EXPECT_NE(found_child, root_nodes.end());
}

NOLINT_TEST_F(SceneReparentTest, MakeNodeRoot_PreservesSceneNodeCount)
{
  // Arrange: Create parent-child hierarchy
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  const auto initial_node_count = scene_->GetNodeCount();

  // Act: Make child a root node
  const auto result = scene_->MakeNodeRoot(child, false);

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

  for (const auto& child : children) {
    EXPECT_FALSE(child.IsRoot());
    EXPECT_TRUE(child.HasParent());
    EXPECT_TRUE(child.IsValid());
  }
}

NOLINT_TEST_F(SceneReparentEdgeTest, MakeNodeRoot_EmptyNameNode_WorksCorrectly)
{
  // Arrange: Create nodes with empty and unusual names
  auto parent = CreateNode(""); // Empty name
  auto child_opt = CreateChildNode(parent, "   "); // Whitespace name
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  // Act: Make child with whitespace name a root
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Should work despite unusual names
  EXPECT_TRUE(result);
  EXPECT_TRUE(child.IsRoot());
  CHECK_FOR_FAILURES_MSG(
    ExpectNodeValidWithName(child, "   "), "child with whitespace name");
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_VeryDeepHierarchy_HandledCorrectly)
{
  // Arrange: Create very deep hierarchy (15 levels)
  SceneNode current = CreateNode("Root");
  std::vector<SceneNode> hierarchy = { current };

  for (int i = 1; i < 15; ++i) {
    auto child_opt = CreateChildNode(current, "Level" + std::to_string(i));
    ASSERT_TRUE(child_opt.has_value());
    current = *child_opt;
    hierarchy.push_back(current);
  }

  // Act: Make deep child a root (moving 14-level subtree)
  const auto middle_node = hierarchy[7]; // Node at level 7
  const auto result = scene_->MakeNodeRoot(middle_node, false);

  // Assert: Should handle deep hierarchy correctly
  EXPECT_TRUE(result);
  EXPECT_TRUE(middle_node.IsRoot());

  // Verify subtree structure is preserved
  for (size_t i = 8; i < hierarchy.size(); ++i) {
    EXPECT_FALSE(hierarchy[i].IsRoot());
    EXPECT_TRUE(hierarchy[i].IsValid());
  }
}

NOLINT_TEST_F(
  SceneReparentEdgeTest, MakeNodeRoot_ImmediatelyAfterCreation_WorksCorrectly)
{
  // Arrange: Create child and immediately make it root
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  // Act: Make root immediately without any intervening operations
  const auto result = scene_->MakeNodeRoot(child, false);

  // Assert: Should work correctly even with minimal setup
  EXPECT_TRUE(result);
  EXPECT_TRUE(child.IsRoot());
  EXPECT_TRUE(child.IsValid());
}

NOLINT_TEST_F(SceneReparentEdgeTest,
  MakeNodeRoot_WithTransformPreservation_ZeroScaleHandling)
{
  // Arrange: Create child with zero scale on one axis
  auto parent = CreateNode("Parent");
  auto child_opt = CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = *child_opt;

  SetupNodeTransform(
    parent, MakeVec3(10, 10, 10), QuatFromEuler(0, 0, 0), MakeVec3(1, 1, 1));
  SetupNodeTransform(child, MakeVec3(0, 0, 0), QuatFromEuler(0, 0, 0),
    MakeVec3(0, 1, 1)); // Zero X scale
  UpdateSceneTransforms();

  // Act: Should handle zero scale gracefully
  EXPECT_NO_FATAL_FAILURE({
    const auto result = scene_->MakeNodeRoot(child, true);
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

} // namespace
