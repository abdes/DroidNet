//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
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
using oxygen::scene::detail::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneGraphFunctionalTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Arrange: Create scene for functional testing scenarios
    scene_ = std::make_shared<Scene>("FunctionalTestScene", 2048);
  }

  void TearDown() override
  {
    // Clean up: Reset scene pointer to ensure proper cleanup
    scene_.reset();
  }

  // Helper: Create a game object with specific transform and flags
  [[nodiscard]] auto CreateGameObject(const std::string& name,
    const glm::vec3& position = { 0.0f, 0.0f, 0.0f },
    const glm::vec3& scale = { 1.0f, 1.0f, 1.0f }, const bool visible = true,
    const bool static_obj = false) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(visible))
                         .SetFlag(SceneNodeFlags::kStatic,
                           SceneFlag {}.SetEffectiveValueBit(static_obj));

    auto node = scene_->CreateNode(name, flags);

    // Set transform if not default
    if (position != glm::vec3 { 0.0f, 0.0f, 0.0f }
      || scale != glm::vec3 { 1.0f, 1.0f, 1.0f }) {
      auto transform = node.GetTransform();
      transform.SetLocalPosition(position);
      transform.SetLocalScale(scale);
    }

    return node;
  }

  // Helper: Create a hierarchy for testing (parent with children)
  [[nodiscard]] auto CreateHierarchy(const std::string& parent_name,
    const std::vector<std::string>& child_names) const
    -> std::pair<SceneNode, std::vector<SceneNode>>
  {
    auto parent = CreateGameObject(parent_name);
    auto children = std::vector<SceneNode> {};

    for (const auto& child_name : child_names) {
      auto child_opt = scene_->CreateChildNode(parent, child_name);
      EXPECT_TRUE(child_opt.has_value())
        << "Failed to create child: " << child_name;
      if (child_opt.has_value()) {
        children.push_back(child_opt.value());
      }
    }

    return { parent, children };
  }

  // Helper: Verify node has expected transform values
  static void ExpectTransformValues(const SceneNode& node,
    const glm::vec3& expected_pos, const glm::vec3& expected_scale)
  {
    const auto transform = node.GetTransform();
    const auto pos = transform.GetLocalPosition();
    const auto scale = transform.GetLocalScale();

    ASSERT_TRUE(pos.has_value());
    ASSERT_TRUE(scale.has_value());
    EXPECT_EQ(*pos, expected_pos);
    EXPECT_EQ(*scale, expected_scale);
  }

  // Helper: Verify scene integrity (no dangling references)
  void VerifySceneIntegrity() const
  {
    for (const auto root_nodes = scene_->GetRootHandles();
      const auto& root_handle : root_nodes) {
      auto root_opt = scene_->GetNode(root_handle);
      ASSERT_TRUE(root_opt.has_value());
      VerifyNodeHierarchyIntegrity(root_opt.value());
    }
  }

  // Helper: Recursively verify node hierarchy integrity
  static void VerifyNodeHierarchyIntegrity(SceneNode& node)
  {
    EXPECT_TRUE(node.IsValid());

    // Verify parent-child consistency
    if (auto parent = node.GetParent(); parent.has_value()) {
      EXPECT_FALSE(node.IsRoot());
      // Parent should have this node as one of its children
      bool found_in_parent = false;
      auto current_child = parent->GetFirstChild();
      while (current_child.has_value()) {
        if (current_child->GetHandle() == node.GetHandle()) {
          found_in_parent = true;
          break;
        }
        current_child = current_child->GetNextSibling();
      }
      EXPECT_TRUE(found_in_parent)
        << "Node not found in parent's children list";
    } else {
      EXPECT_TRUE(node.IsRoot());
    }

    // Verify sibling consistency
    if (auto next_sibling = node.GetNextSibling(); next_sibling.has_value()) {
      auto prev_of_next = next_sibling->GetPrevSibling();
      ASSERT_TRUE(prev_of_next.has_value());
      EXPECT_EQ(prev_of_next->GetHandle(), node.GetHandle());
    }

    if (auto prev_sibling = node.GetPrevSibling(); prev_sibling.has_value()) {
      auto next_of_prev = prev_sibling->GetNextSibling();
      ASSERT_TRUE(next_of_prev.has_value());
      EXPECT_EQ(next_of_prev->GetHandle(), node.GetHandle());
    }

    // Recursively verify all children
    auto child = node.GetFirstChild();
    while (child.has_value()) {
      VerifyNodeHierarchyIntegrity(child.value());
      child = child->GetNextSibling();
    }
  }

  std::shared_ptr<Scene> scene_;
};

//------------------------------------------------------------------------------
// Basic Node Lifecycle Functional Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphFunctionalTest, NodeLifecycle_CreateModifyDestroy)
{
  // Arrange: Start with empty scene
  EXPECT_EQ(scene_->GetNodeCount(), 0);

  // Act: Create a game object
  auto player
    = CreateGameObject("Player", { 10.0f, 5.0f, 0.0f }, { 1.5f, 1.5f, 1.5f });

  // Assert: Node should be created correctly
  EXPECT_TRUE(player.IsValid());
  EXPECT_EQ(scene_->GetNodeCount(), 1);
  ExpectTransformValues(player, { 10.0f, 5.0f, 0.0f }, { 1.5f, 1.5f, 1.5f });

  // Act: Modify the game object
  const auto impl_opt = player.GetObject();
  ASSERT_TRUE(impl_opt.has_value());
  auto& impl = impl_opt->get();
  impl.SetName("MainPlayer");

  auto transform = player.GetTransform();
  transform.SetLocalPosition({ 20.0f, 10.0f, 5.0f });

  // Assert: Modifications should be preserved
  EXPECT_EQ(impl.GetName(), "MainPlayer");
  ExpectTransformValues(player, { 20.0f, 10.0f, 5.0f }, { 1.5f, 1.5f, 1.5f });

  // Act: Destroy the node
  scene_->DestroyNode(player);

  // Assert: Node should be invalidated and scene should be empty
  EXPECT_FALSE(player.IsValid());
  EXPECT_EQ(scene_->GetNodeCount(), 0);
}

NOLINT_TEST_F(
  SceneGraphFunctionalTest, NodePersistence_HandleValidityAcrossOperations)
{
  // Arrange: Create multiple nodes
  const auto enemy1 = CreateGameObject("Enemy1");
  auto enemy2 = CreateGameObject("Enemy2");
  const auto enemy3 = CreateGameObject("Enemy3");

  const auto enemy1_handle = enemy1.GetHandle();
  const auto enemy2_handle = enemy2.GetHandle();

  // Act: Store handles and retrieve nodes later
  const auto retrieved_enemy1_opt = scene_->GetNode(enemy1_handle);
  const auto retrieved_enemy2_opt = scene_->GetNode(enemy2_handle);

  // Assert: Retrieved nodes should be valid and match originals
  ASSERT_TRUE(retrieved_enemy1_opt.has_value());
  ASSERT_TRUE(retrieved_enemy2_opt.has_value());
  EXPECT_EQ(retrieved_enemy1_opt->GetHandle(), enemy1.GetHandle());
  EXPECT_EQ(retrieved_enemy2_opt->GetHandle(), enemy2.GetHandle());

  // Act: Destroy one node and verify handles update appropriately
  scene_->DestroyNode(enemy2);

  // Assert: Destroyed node handle should be invalid, others should remain
  // valid
  EXPECT_FALSE(scene_->GetNode(enemy2_handle).has_value());
  EXPECT_TRUE(scene_->GetNode(enemy1_handle).has_value());
  EXPECT_TRUE(enemy1.IsValid());
  EXPECT_FALSE(enemy2.IsValid());
  EXPECT_TRUE(enemy3.IsValid());
}

//------------------------------------------------------------------------------
// Hierarchy Management Functional Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneGraphFunctionalTest, GameObjectHierarchy_ParentChildRelationships)
{
  // Arrange: Create a vehicle hierarchy (Vehicle -> Body, Wheels)
  auto [vehicle, parts] = CreateHierarchy("Vehicle",
    { "Body", "FrontLeftWheel", "FrontRightWheel", "RearLeftWheel",
      "RearRightWheel" });
  ASSERT_EQ(parts.size(), 5);

  // Assert: Verify hierarchy structure
  EXPECT_TRUE(vehicle.IsRoot());
  EXPECT_TRUE(vehicle.HasChildren());
  EXPECT_EQ(scene_->GetChildrenCount(vehicle), 5);

  for (auto& part : parts) {
    EXPECT_FALSE(part.IsRoot());
    EXPECT_TRUE(part.HasParent());
    EXPECT_FALSE(part.HasChildren());

    auto parent = part.GetParent();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->GetHandle(), vehicle.GetHandle());
  }

  // Act: Move vehicle and verify all parts move with it (conceptually)
  auto vehicle_transform = vehicle.GetTransform();
  vehicle_transform.SetLocalPosition({ 100.0f, 0.0f, 50.0f });

  // Assert: Verify scene integrity after modification
  VerifySceneIntegrity();
}

NOLINT_TEST_F(SceneGraphFunctionalTest, ComplexHierarchy_MultiLevelNesting)
{
  // Arrange: Create a complex scene hierarchy step by step
  // World -> Player -> (Head, Body -> (LeftArm, RightArm), Legs -> (LeftLeg,
  // RightLeg))
  auto world = CreateGameObject("World");

  // Act: Create Player as child of World
  auto player_opt = scene_->CreateChildNode(world, "Player");
  ASSERT_TRUE(player_opt.has_value());
  auto& player = player_opt.value();

  // Act: Create Player's main body parts
  auto head_opt = scene_->CreateChildNode(player, "Head");
  auto body_opt = scene_->CreateChildNode(player, "Body");
  auto legs_opt = scene_->CreateChildNode(player, "Legs");

  ASSERT_TRUE(head_opt.has_value());
  ASSERT_TRUE(body_opt.has_value());
  ASSERT_TRUE(legs_opt.has_value());

  auto& head = head_opt.value();
  auto& body = body_opt.value();
  auto& legs = legs_opt.value();

  // Act: Create arms under body
  auto left_arm_opt = scene_->CreateChildNode(body, "LeftArm");
  auto right_arm_opt = scene_->CreateChildNode(body, "RightArm");
  ASSERT_TRUE(left_arm_opt.has_value());
  ASSERT_TRUE(right_arm_opt.has_value());

  // Act: Create leg parts under legs
  auto left_leg_opt = scene_->CreateChildNode(legs, "LeftLeg");
  auto right_leg_opt = scene_->CreateChildNode(legs, "RightLeg");
  ASSERT_TRUE(left_leg_opt.has_value());
  ASSERT_TRUE(right_leg_opt.has_value());

  // Assert: Verify final total node count (World + Player + Head + Body +
  // Legs + LeftArm + RightArm + LeftLeg + RightLeg = 9)
  EXPECT_EQ(scene_->GetNodeCount(), 9)
    << "Final count: World + Player + Head + Body + Legs + 2 Arms + 2 Legs "
       "= 9 nodes";

  // Assert: Verify hierarchy structure at each level
  EXPECT_TRUE(world.IsRoot()) << "World should be root";
  EXPECT_FALSE(world.HasParent()) << "World should have no parent";
  EXPECT_TRUE(world.HasChildren()) << "World should have children";
  EXPECT_EQ(scene_->GetChildrenCount(world), 1) << "World should have 1 child";

  EXPECT_FALSE(player.IsRoot()) << "Player should not be root";
  EXPECT_TRUE(player.HasParent()) << "Player should have parent";
  EXPECT_TRUE(player.HasChildren()) << "Player should have children";
  EXPECT_EQ(scene_->GetChildrenCount(player), 3)
    << "Player should have 3 children";

  EXPECT_FALSE(body.IsRoot()) << "Body should not be root";
  EXPECT_TRUE(body.HasParent()) << "Body should have parent";
  EXPECT_TRUE(body.HasChildren()) << "Body should have children";
  EXPECT_EQ(scene_->GetChildrenCount(body), 2) << "Body should have 2 children";

  EXPECT_FALSE(legs.IsRoot()) << "Legs should not be root";
  EXPECT_TRUE(legs.HasParent()) << "Legs should have parent";
  EXPECT_TRUE(legs.HasChildren()) << "Legs should have children";
  EXPECT_EQ(scene_->GetChildrenCount(legs), 2) << "Legs should have 2 children";

  EXPECT_FALSE(head.IsRoot()) << "Head should not be root";
  EXPECT_TRUE(head.HasParent()) << "Head should have parent";
  EXPECT_FALSE(head.HasChildren()) << "Head should have no children";
  EXPECT_EQ(scene_->GetChildrenCount(head), 0) << "Head should have 0 children";

  // Assert: Verify only World should be in root nodes collection
  auto final_root_nodes = scene_->GetRootHandles();
  EXPECT_EQ(final_root_nodes.size(), 1) << "Should have exactly 1 root node";
  EXPECT_EQ(final_root_nodes[0], world.GetHandle())
    << "World should be the only root node";

  // Assert: Verify scene integrity after complex hierarchy creation
  VerifySceneIntegrity();
}

NOLINT_TEST_F(SceneGraphFunctionalTest, HierarchyDestruction_EntireSceneGraph)
{
  // Arrange: Create a hierarchy with nested objects
  auto [root, children]
    = CreateHierarchy("RootObject", { "Child1", "Child2", "Child3" });

  // Add grandchildren to first child
  auto child1 = children[0];
  auto grandchild1_opt = scene_->CreateChildNode(child1, "GrandChild1");
  auto grandchild2_opt = scene_->CreateChildNode(child1, "GrandChild2");
  ASSERT_TRUE(grandchild1_opt.has_value());
  ASSERT_TRUE(grandchild2_opt.has_value());

  auto grandchild1 = grandchild1_opt.value();
  auto grandchild2 = grandchild2_opt.value();

  EXPECT_EQ(scene_->GetNodeCount(), 6); // root + 3 children + 2 grandchildren

  // Act: Destroy the root hierarchy
  auto destruction_result = scene_->DestroyNodeHierarchy(root);

  // Assert: All nodes should be destroyed
  EXPECT_TRUE(destruction_result);
  EXPECT_EQ(scene_->GetNodeCount(), 0);

  // Assert: All node handles should be invalid
  EXPECT_FALSE(root.GetObject().has_value());
  EXPECT_FALSE(child1.GetObject().has_value());
  EXPECT_FALSE(children[1].GetObject().has_value());
  EXPECT_FALSE(children[2].GetObject().has_value());
  EXPECT_FALSE(grandchild1.GetObject().has_value());
  EXPECT_FALSE(grandchild2.GetObject().has_value());
}

NOLINT_TEST_F(
  SceneGraphFunctionalTest, HierarchyDestruction_SubtreeInMultiRootScene)
{
  // Arrange: Create a multi-root scene graph with multiple independent
  // hierarchies
  auto [root1, children1]
    = CreateHierarchy("RootObject1", { "Child1A", "Child1B" });
  auto [root2, children2]
    = CreateHierarchy("RootObject2", { "Child2A", "Child2B", "Child2C" });
  auto [root3, children3] = CreateHierarchy("RootObject3", { "Child3A" });

  // Create grandchildren under Child1A to make it a deeper subtree
  auto grandchild1A1_opt
    = scene_->CreateChildNode(children1[0], "GrandChild1A1");
  auto grandchild1A2_opt
    = scene_->CreateChildNode(children1[0], "GrandChild1A2");
  ASSERT_TRUE(grandchild1A1_opt.has_value());
  ASSERT_TRUE(grandchild1A2_opt.has_value());

  auto grandchild1A1 = grandchild1A1_opt.value();
  auto grandchild1A2 = grandchild1A2_opt.value();

  // Arrange: Verify initial scene state
  EXPECT_EQ(scene_->GetNodeCount(),
    11); // 3 roots + 6 children + 2 grandchildren = 11
  auto initial_roots = scene_->GetRootHandles();
  EXPECT_EQ(initial_roots.size(), 3) << "Should have exactly 3 root nodes";

  // Verify each root has expected children
  EXPECT_EQ(scene_->GetChildrenCount(root1), 2)
    << "Root1 should have 2 children";
  EXPECT_EQ(scene_->GetChildrenCount(root2), 3)
    << "Root2 should have 3 children";
  EXPECT_EQ(scene_->GetChildrenCount(root3), 1) << "Root3 should have 1 child";

  // Verify Child1A has grandchildren
  EXPECT_EQ(scene_->GetChildrenCount(children1[0]), 2)
    << "Child1A should have 2 grandchildren";

  // Act: Destroy Child1A subtree (which includes its grandchildren)
  auto destruction_result = scene_->DestroyNodeHierarchy(children1[0]);

  // Assert: Only the Child1A subtree should be destroyed (Child1A + 2
  // grandchildren = 3 nodes)
  EXPECT_TRUE(destruction_result);
  EXPECT_EQ(scene_->GetNodeCount(), 8)
    << "Should have 11 - 3 = 8 nodes remaining";

  // Assert: Root1 and its other children should still exist
  EXPECT_TRUE(root1.IsValid());
  EXPECT_TRUE(children1[1].IsValid()) << "Child1B should still exist";

  // Assert: Root1 should still be a root but with fewer children
  EXPECT_TRUE(root1.IsRoot());
  EXPECT_EQ(scene_->GetChildrenCount(root1), 1)
    << "Root1 should now have 1 child (Child1B)";

  // Assert: Other root hierarchies should be completely unaffected
  EXPECT_TRUE(root2.IsValid());
  EXPECT_TRUE(children2[0].IsValid()) << "Child2A should still exist";
  EXPECT_TRUE(children2[1].IsValid()) << "Child2B should still exist";
  EXPECT_TRUE(children2[2].IsValid()) << "Child2C should still exist";
  EXPECT_EQ(scene_->GetChildrenCount(root2), 3)
    << "Root2 should still have 3 children";

  EXPECT_TRUE(root3.IsValid());
  EXPECT_TRUE(children3[0].IsValid()) << "Child3A should still exist";
  EXPECT_EQ(scene_->GetChildrenCount(root3), 1)
    << "Root3 should still have 1 child";

  // Assert: Scene should still have 3 root nodes
  auto final_roots = scene_->GetRootHandles();
  EXPECT_EQ(final_roots.size(), 3) << "Should still have exactly 3 root nodes";

  // Assert: Destroyed subtree nodes should be invalid
  EXPECT_FALSE(children1[0].GetObject().has_value())
    << "Child1A should be destroyed";
  EXPECT_FALSE(grandchild1A1.GetObject().has_value())
    << "GrandChild1A1 should be destroyed";
  EXPECT_FALSE(grandchild1A2.GetObject().has_value())
    << "GrandChild1A2 should be destroyed";

  // Assert: Verify scene integrity after partial destruction
  VerifySceneIntegrity();
}

//------------------------------------------------------------------------------
// Transform System Integration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphFunctionalTest, TransformHierarchy_WorldSpaceTransforms)
{
  // Arrange: Create parent-child hierarchy with transforms
  auto parent
    = CreateGameObject("Parent", { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = child_opt.value();

  auto child_transform = child.GetTransform();
  child_transform.SetLocalPosition({ 5.0f, 10.0f, 15.0f });
  child_transform.SetLocalScale({ 0.5f, 0.5f, 0.5f });

  // Act: Update transforms to compute world space values
  const auto parent_impl_opt = parent.GetObject();
  const auto child_impl_opt = child.GetObject();
  ASSERT_TRUE(parent_impl_opt.has_value());
  ASSERT_TRUE(child_impl_opt.has_value());

  const auto& parent_impl = parent_impl_opt->get();
  const auto& child_impl = child_impl_opt->get();

  // Update parent transform first (root)
  parent_impl.GetComponent<TransformComponent>().UpdateWorldTransformAsRoot();

  // Update child transform with parent matrix
  const auto& parent_transform_component
    = parent_impl.GetComponent<TransformComponent>();
  auto& child_transform_component
    = child_impl.GetComponent<TransformComponent>();
  child_transform_component.UpdateWorldTransform(
    parent_transform_component.GetWorldMatrix());

  // Assert: Verify world space computations
  const auto child_world_pos = child_transform_component.GetWorldPosition();
  const auto child_world_scale = child_transform_component.GetWorldScale();

  // Expected: parent_pos + (parent_rotation * (parent_scale * child_pos))
  // Since no rotation: (10,20,30) + (2,2,2) * (5,10,15) = (10,20,30) +
  // (10,20,30) = (20,40,60)
  EXPECT_EQ(child_world_pos, glm::vec3(20.0f, 40.0f, 60.0f));

  // Expected: parent_scale * child_scale = (2,2,2) * (0.5,0.5,0.5) = (1,1,1)
  EXPECT_EQ(child_world_scale, glm::vec3(1.0f, 1.0f, 1.0f));
}

NOLINT_TEST_F(SceneGraphFunctionalTest, TransformOperations_LocalAndWorldSpace)
{
  // Arrange: Create object with specific transform
  auto object = CreateGameObject("MovableObject", { 0.0f, 0.0f, 0.0f });
  auto transform = object.GetTransform();

  // Act: Perform various transform operations
  transform.SetLocalPosition({ 10.0f, 0.0f, 0.0f });

  // Apply local translation (should be in object's local space)
  const auto impl_opt = object.GetObject();
  ASSERT_TRUE(impl_opt.has_value());
  auto& transform_component
    = impl_opt->get().GetComponent<TransformComponent>();

  // Set rotation first
  const auto rotation = glm::quat(
    glm::radians(glm::vec3(0.0f, 90.0f, 0.0f))); // 90 degrees around Y
  transform_component.SetLocalRotation(rotation);

  // Translate in local space (should be rotated)
  transform_component.Translate({ 5.0f, 0.0f, 0.0f }, true); // Local space

  // Assert: Verify the result accounts for rotation
  const auto final_position = transform_component.GetLocalPosition();
  // Original position (10,0,0) + rotated offset (90° Y rotation of (5,0,0) =
  // (0,0,-5)) = (10,0,-5)
  EXPECT_NEAR(final_position.x, 10.0f, 1e-5f);
  EXPECT_NEAR(final_position.y, 0.0f, 1e-5f);
  EXPECT_NEAR(final_position.z, -5.0f, 1e-5f);
}

//------------------------------------------------------------------------------
// Flag System Integration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneGraphFunctionalTest, FlagInheritance_ParentToChildPropagation)
{
  // Arrange: Create parent with specific flags
  const auto parent_flags = SceneNode::Flags {}
                              .SetFlag(SceneNodeFlags::kVisible,
                                SceneFlag {}.SetEffectiveValueBit(true))
                              .SetFlag(SceneNodeFlags::kCastsShadows,
                                SceneFlag {}.SetEffectiveValueBit(true));

  auto parent = scene_->CreateNode("Parent", parent_flags);
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = child_opt.value();

  // Act: Set child flags to inherit from parent
  const auto child_impl_opt = child.GetObject();
  ASSERT_TRUE(child_impl_opt.has_value());
  auto& child_impl = child_impl_opt->get();

  auto& child_flags = child_impl.GetFlags();
  child_flags.SetInherited(SceneNodeFlags::kCastsShadows, true);

  // Simulate parent flag update
  const auto parent_impl_opt = parent.GetObject();
  ASSERT_TRUE(parent_impl_opt.has_value());
  auto& parent_impl = parent_impl_opt->get();
  const auto& parent_flags_ref = parent_impl.GetFlags();

  child_flags.UpdateValueFromParent(
    SceneNodeFlags::kCastsShadows, parent_flags_ref);
  child_flags.ProcessDirtyFlags();

  // Assert: Child should inherit parent's shadow casting flag
  EXPECT_TRUE(child_flags.IsInherited(SceneNodeFlags::kCastsShadows));
  EXPECT_TRUE(child_flags.GetEffectiveValue(SceneNodeFlags::kCastsShadows));
}

NOLINT_TEST_F(SceneGraphFunctionalTest, FlagModification_DynamicFlagChanges)
{
  // Arrange: Create game objects with different visibility states
  auto visible_object = CreateGameObject(
    "VisibleObject", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, true);
  auto hidden_object = CreateGameObject(
    "HiddenObject", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, false);

  // Act: Toggle visibility flags
  const auto visible_impl_opt = visible_object.GetObject();
  const auto hidden_impl_opt = hidden_object.GetObject();
  ASSERT_TRUE(visible_impl_opt.has_value());
  ASSERT_TRUE(hidden_impl_opt.has_value());

  auto& visible_flags = visible_impl_opt->get().GetFlags();
  auto& hidden_flags = hidden_impl_opt->get().GetFlags();

  // Toggle visibility
  visible_flags.SetLocalValue(SceneNodeFlags::kVisible, false);
  hidden_flags.SetLocalValue(SceneNodeFlags::kVisible, true);

  visible_flags.ProcessDirtyFlags();
  hidden_flags.ProcessDirtyFlags();

  // Assert: Flags should be updated correctly
  EXPECT_FALSE(visible_flags.GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_TRUE(hidden_flags.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Performance and Scale Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphFunctionalTest, LargeSceneManagement_ManyObjects)
{
  // Arrange: Create a large number of objects
  constexpr std::size_t object_count = 1000;
  auto objects = std::vector<SceneNode> {};
  objects.reserve(object_count);

  // Act: Create many game objects
  for (std::size_t i = 0; i < object_count; ++i) {
    auto object_name = "GameObject_" + std::to_string(i);
    auto position = glm::vec3 { static_cast<float>(i % 100),
      static_cast<float>(i / 100 % 100), static_cast<float>(i / 10000) };
    auto object = CreateGameObject(object_name, position);
    objects.push_back(object);
  }

  // Assert: Verify all objects are created and accessible
  EXPECT_EQ(scene_->GetNodeCount(), object_count);
  EXPECT_EQ(objects.size(), object_count);

  // Verify random access to objects
  for (std::size_t i = 0; i < 10; ++i) {
    const auto random_index = i * 101 % object_count; // Pseudo-random access
    auto& object = objects[random_index];
    EXPECT_TRUE(object.IsValid());

    auto expected_pos = glm::vec3 { static_cast<float>(random_index % 100),
      static_cast<float>(random_index / 100 % 100),
      static_cast<float>(random_index / 10000) };
    ExpectTransformValues(object, expected_pos, { 1.0f, 1.0f, 1.0f });
  }

  // Act: Clean up by destroying all objects
  for (auto& object : objects) {
    scene_->DestroyNode(object);
  }

  // Assert: Scene should be empty
  EXPECT_EQ(scene_->GetNodeCount(), 0);
}

NOLINT_TEST_F(SceneGraphFunctionalTest, DeepHierarchy_ExtensiveNesting)
{
  // Arrange: Create a deep hierarchy (chain of 50 nested objects)
  constexpr int depth = 50;
  auto current_parent = CreateGameObject("Root");
  auto all_nodes = std::vector { current_parent };

  // Act: Create deep nesting
  for (int i = 1; i < depth; ++i) {
    auto child_name = "Level_" + std::to_string(i);
    auto child_opt = scene_->CreateChildNode(current_parent, child_name);
    ASSERT_TRUE(child_opt.has_value());
    current_parent = child_opt.value();
    all_nodes.push_back(current_parent);
  }

  // Assert: Verify deep hierarchy navigation
  EXPECT_EQ(scene_->GetNodeCount(), depth);

  // Navigate from root to leaf
  auto current = all_nodes[0]; // Root
  for (int i = 1; i < depth; ++i) {
    EXPECT_TRUE(current.HasChildren());
    auto child = current.GetFirstChild();
    ASSERT_TRUE(child.has_value());
    current = child.value();
    EXPECT_EQ(current.GetHandle(), all_nodes[i].GetHandle());
  }

  // Navigate from leaf to root
  current = all_nodes[depth - 1]; // Leaf
  for (int i = depth - 2; i >= 0; --i) {
    EXPECT_TRUE(current.HasParent());
    auto parent = current.GetParent();
    ASSERT_TRUE(parent.has_value());
    current = parent.value();
    EXPECT_EQ(current.GetHandle(), all_nodes[i].GetHandle());
  }

  // Assert: Verify scene integrity
  VerifySceneIntegrity();
}

//------------------------------------------------------------------------------
// Error Handling and Edge Cases Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  SceneGraphFunctionalTest, ErrorRecovery_InvalidOperationsHandledGracefully)
{
  // Arrange: Create valid objects
  auto valid_object = CreateGameObject("ValidObject");
  auto another_object = CreateGameObject("AnotherObject");

  // Act: Attempt invalid operations
  const auto invalid_handle = NodeHandle {}; // Default invalid handle
  const auto invalid_node_opt = scene_->GetNode(invalid_handle);

  // Assert: Invalid operations should fail gracefully
  EXPECT_FALSE(invalid_node_opt.has_value());

  // Act: Destroy object and attempt operations on invalid node
  scene_->DestroyNode(valid_object);
  const auto destroyed_flags = valid_object.GetFlags();
  const auto destroyed_transform = valid_object.GetTransform();

  // Assert: Operations on destroyed nodes should fail gracefully
  EXPECT_FALSE(destroyed_flags.has_value());
  EXPECT_FALSE(destroyed_transform.GetLocalPosition().has_value());

  // Assert: Other objects should remain unaffected
  EXPECT_TRUE(another_object.IsValid());
  EXPECT_TRUE(another_object.GetFlags().has_value());
}

NOLINT_TEST_F(
  SceneGraphFunctionalTest, MemoryManagement_SequentialCreationAndDestruction)
{
  // Arrange: Test sequential creation and destruction to avoid Scene state
  // issues
  constexpr std::size_t total_objects = 100;

  for (std::size_t i = 0; i < total_objects; ++i) {
    // Act: Create single object
    auto object_name = "SequentialObject_" + std::to_string(i);
    auto object = CreateGameObject(object_name);

    // Assert: Verify object is created correctly
    ASSERT_TRUE(object.IsValid())
      << "Object " << i << " should be created successfully";
    EXPECT_EQ(scene_->GetNodeCount(), 1) << "Scene should have exactly 1 node";

    // Act: Modify object to test functionality
    auto transform = object.GetTransform();
    transform.SetLocalPosition({ static_cast<float>(i), 0.0f, 0.0f });

    // Act: Verify modification worked
    auto position = transform.GetLocalPosition();
    ASSERT_TRUE(position.has_value());
    EXPECT_FLOAT_EQ(position->x, static_cast<float>(i));

    // Act: Destroy object immediately
    auto destruction_result = scene_->DestroyNode(object);

    // Assert: Destruction should succeed and scene should be empty
    EXPECT_TRUE(destruction_result)
      << "Object " << i << " should be destroyed successfully";
    EXPECT_EQ(scene_->GetNodeCount(), 0)
      << "Scene should be empty after destroying object " << i;
    EXPECT_TRUE(scene_->GetRootHandles().empty())
      << "Root nodes should be empty after destroying object " << i;
  }

  // Assert: Final verification
  EXPECT_EQ(scene_->GetNodeCount(), 0);
  EXPECT_TRUE(scene_->GetRootHandles().empty());
}

NOLINT_TEST_F(
  SceneGraphFunctionalTest, MemoryManagement_NoLeaksAfterBulkOperations)
{
  // Arrange: Perform bulk creation and destruction with validation
  constexpr std::size_t iterations
    = 10; // Reduced to avoid Scene state corruption

  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    constexpr std::size_t objects_per_iteration = 5;
    auto objects = std::vector<SceneNode> {};

    // Act: Create objects
    for (std::size_t i = 0; i < objects_per_iteration; ++i) {
      auto object_name
        = "TempObject_" + std::to_string(iteration) + "_" + std::to_string(i);
      auto object = CreateGameObject(object_name);

      // Assert: Verify object creation succeeded before adding to
      // collection
      ASSERT_TRUE(object.IsValid())
        << "Object creation should succeed for " << object_name;
      objects.push_back(object);
    }

    // Assert: Verify expected scene state after creation
    EXPECT_EQ(scene_->GetNodeCount(), objects_per_iteration)
      << "Scene should have exactly " << objects_per_iteration
      << " nodes after creation in iteration " << iteration;

    // Act: Modify objects (test that they're functional)
    for (auto& object : objects) {
      auto transform = object.GetTransform();
      auto position = glm::vec3 { static_cast<float>(iteration),
        static_cast<float>(iteration), static_cast<float>(iteration) };

      // Only modify if object is still valid
      if (object.IsValid()) {
        transform.SetLocalPosition(position);
      }
    }

    // Act: Destroy all objects one by one
    for (auto& object : objects) {
      if (object.IsValid()) {
        auto destruction_result = scene_->DestroyNode(object);
        EXPECT_TRUE(destruction_result)
          << "Node destruction should succeed in iteration " << iteration;
      }
    }

    // Assert: Scene should be empty after each iteration
    EXPECT_EQ(scene_->GetNodeCount(), 0)
      << "Scene should be empty after iteration " << iteration;

    // Assert: Verify root nodes collection is also empty
    auto root_nodes = scene_->GetRootHandles();
    EXPECT_TRUE(root_nodes.empty())
      << "Root nodes collection should be empty after iteration " << iteration;
  }

  // Assert: Final scene state should be clean
  EXPECT_EQ(scene_->GetNodeCount(), 0);
  EXPECT_TRUE(scene_->GetRootHandles().empty());
}

} // namespace

//------------------------------------------------------------------------------
// Additional Cross-Module Scenarios Needed:
//------------------------------------------------------------------------------
/*
 * Based on this refactoring, the following additional cross-module test
 * scenarios should be implemented in separate test files:
 *
 * 1. **Scene Serialization Tests** (Scene + Serialization Module):
 *    - Save/load complete scene hierarchies with transforms and flags
 *    - Incremental scene updates and delta serialization
 *    - Cross-platform scene file compatibility
 *
 * 2. **Rendering Integration Tests** (Scene + Rendering Module):
 *    - Visibility culling based on SceneNodeFlags::kVisible
 *    - Shadow casting/receiving based on shadow flags
 *    - Transform hierarchy affecting render matrices
 *    - Dynamic object addition/removal during rendering
 *
 * 3. **Physics Integration Tests** (Scene + Physics Module):
 *    - Static objects (SceneNodeFlags::kStatic) in physics world
 *    - Transform synchronization between scene and physics
 *    - Collision shape updates when transforms change
 *    - Hierarchy constraints affecting physics bodies
 *
 * 4. **Asset Loading Tests** (Scene + Asset Management):
 *    - Loading prefabs and instantiating as scene hierarchies
 *    - Asset dependency tracking for scene objects
 *    - Hot-reloading assets and updating scene objects
 *
 * 5. **Scripting Integration Tests** (Scene + Scripting Module):
 *    - Script component attachment to scene nodes
 *    - Scene graph navigation from scripts
 *    - Event propagation through hierarchy
 *
 * 6. **Multi-threading Tests** (Scene + Threading):
 *    - Concurrent read operations on scene graph
 *    - Thread-safe flag updates using AtomicSceneFlags
 *    - Transform updates from multiple threads
 *
 * 7. **Performance Profiling Tests**:
 *    - Large scene hierarchy traversal performance
 *    - Memory usage patterns for different scene structures
 *    - Cache-friendly node access patterns
 *
 * These scenarios would exercise the Scene module in realistic usage patterns
 * and help ensure proper integration with other engine modules.
 */
