//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>

namespace oxygen::scene::testing {

class SceneTest : public ::testing::Test {
public:
  std::shared_ptr<Scene> scene_;

protected:
  //=== Fixture management ===------------------------------------------------//

  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene", 1024); }
  void TearDown() override { scene_.reset(); }

  //=== Node Creation Helpers ===---------------------------------------------//

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

  [[nodiscard]] auto CreateChildNode(const SceneNode& parent,
      const std::string& name, const SceneNode::Flags& flags) const
      -> std::optional<SceneNode>
  {
    return scene_->CreateChildNode(parent, name, flags);
  }

  [[nodiscard]] auto CreateNodeWithInvalidScene() const -> SceneNode
  {
    return {};
  }

  [[nodiscard]] auto CreateNodeWithInvalidHandle() const -> SceneNode
  {
    return SceneNode(scene_);
  }

  // Helper to create a to-be lazily invalidated node for testing. Creates a
  // node, stores its handle, then destroys it, and returns a new node with the
  // stored handle.
  [[nodiscard]] auto CreateLazyInvalidationNode(const std::string& name
      = "InvalidNode") const -> SceneNode
  {
    auto node = scene_->CreateNode(name);
    const auto handle = node.GetHandle();
    scene_->DestroyNode(node);
    return { scene_, handle };
  }

  //=== Node Creation with Flags ===------------------------------------------//

  //! Creates a node with specific visibility setting.
  [[nodiscard]] auto CreateVisibleNode(
      const std::string& name, bool visible = true) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                           .SetFlag(SceneNodeFlags::kVisible,
                               SceneFlag {}.SetEffectiveValueBit(visible))
                           .SetFlag(SceneNodeFlags::kStatic,
                               SceneFlag {}.SetEffectiveValueBit(false));
    return scene_->CreateNode(name, flags);
  }

  //! Creates an invisible node.
  [[nodiscard]] auto CreateInvisibleNode(const std::string& name) const
      -> SceneNode
  {
    return CreateVisibleNode(name, false);
  }

  //! Creates a static node.
  [[nodiscard]] auto CreateStaticNode(const std::string& name) const
      -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                           .SetFlag(SceneNodeFlags::kVisible,
                               SceneFlag {}.SetEffectiveValueBit(true))
                           .SetFlag(SceneNodeFlags::kStatic,
                               SceneFlag {}.SetEffectiveValueBit(true));
    return scene_->CreateNode(name, flags);
  }

  //! Creates a child node with specific visibility.
  [[nodiscard]] auto CreateVisibleChildNode(const SceneNode& parent,
      const std::string& name, bool visible = true) const
      -> std::optional<SceneNode>
  {
    const auto flags = SceneNode::Flags {}
                           .SetFlag(SceneNodeFlags::kVisible,
                               SceneFlag {}.SetEffectiveValueBit(visible))
                           .SetFlag(SceneNodeFlags::kStatic,
                               SceneFlag {}.SetEffectiveValueBit(false));
    return scene_->CreateChildNode(parent, name, flags);
  }

  //! Creates an invisible child node.
  [[nodiscard]] auto CreateInvisibleChildNode(const SceneNode& parent,
      const std::string& name) const -> std::optional<SceneNode>
  {
    return CreateVisibleChildNode(parent, name, false);
  }

  //=== Scene Graph Helpers ===-----------------------------------------------//

  auto DestroyNode(SceneNode& node) const -> bool
  {
    return scene_->DestroyNode(node);
  }

  auto DestroyNodeHierarchy(SceneNode& node) const -> bool
  {
    return scene_->DestroyNodeHierarchy(node);
  }

  void ClearScene() const { scene_->Clear(); }

  //=== Assertion Helpers ===-------------------------------------------------//

  // NB: In order to ensure that a failure in a helper is properly propagated to
  // the test cases that called it, it is important to wrap any call to a helper
  // that includes EXPECT_* or ASSERT_* macros in a CHECK_FOR_FAILURES or
  // CHECK_FOR_FAILURES_MSG macro.
  //
  // See Oxygen/Testing/GTest.h for details.

  //! Validates that a node is valid and has the expected name.
  static void ExpectNodeValidWithName(
      const SceneNode& node, const std::string& name)
  {
    EXPECT_TRUE(node.IsValid());
    const auto obj_opt = node.GetObject();
    EXPECT_TRUE(obj_opt.has_value());
    if (obj_opt.has_value()) {
      EXPECT_EQ(obj_opt->get().GetName(), name);
    }
  }

  //! Validates that a node has been lazy-invalidated (appears valid but object
  //! access fails).
  static void ExpectNodeLazyInvalidated(SceneNode& node)
  {
    // Node may appear valid, but after GetObject() it should be invalidated
    if (node.IsValid()) {
      const auto obj_opt = node.GetObject();
      EXPECT_FALSE(obj_opt.has_value());
      EXPECT_FALSE(node.IsValid());
    }
  }

  //! Validates that a node is not contained in the test scene.
  void ExpectNodeNotInScene(const SceneNode& node) const
  {
    EXPECT_FALSE(scene_->Contains(node));
  }

  //! Validates that multiple node handles are unique.
  static void ExpectHandlesUnique(
      const SceneNode& n1, const SceneNode& n2, const SceneNode& n3)
  {
    EXPECT_NE(n1.GetHandle(), n2.GetHandle());
    EXPECT_NE(n2.GetHandle(), n3.GetHandle());
    EXPECT_NE(n1.GetHandle(), n3.GetHandle());
  }

  //! Validates that the scene is empty.
  void ExpectSceneEmpty() const
  {
    EXPECT_TRUE(scene_->IsEmpty()) << "Scene should be empty";
    EXPECT_EQ(scene_->GetNodeCount(), 0) << "Scene node count should be zero";
  }

  //! Validates that the scene contains exactly the expected number of nodes.
  void ExpectSceneNodeCount(std::size_t expected_count) const
  {
    EXPECT_EQ(scene_->GetNodeCount(), expected_count);
    EXPECT_EQ(scene_->IsEmpty(), expected_count == 0);
  }

  //! Validates that a node is valid, has expected name, and is a root node.
  static void ExpectNodeValidAsRoot(
      const SceneNode& node, const std::string& name)
  {
    ExpectNodeValidWithName(node, name);
    EXPECT_TRUE(node.IsRoot());
    EXPECT_FALSE(node.HasParent());
  }

  //! Validates that a node is valid, has expected parent, and is not a root.
  static void ExpectNodeValidWithParent(
      const SceneNode& node, const SceneNode& expected_parent)
  {
    ASSERT_TRUE(node.IsValid());
    ASSERT_TRUE(expected_parent.IsValid());
    EXPECT_FALSE(node.IsRoot());
    EXPECT_TRUE(node.HasParent());

    const auto parent_opt = node.GetParent();
    ASSERT_TRUE(parent_opt.has_value());

    EXPECT_EQ(parent_opt->GetHandle(), expected_parent.GetHandle());
  }

  //! Validates that a node has no parent (is a root node).
  static void ExpectNodeIsRoot(const SceneNode& node)
  {
    const auto parent_opt = node.GetParent();
    EXPECT_FALSE(parent_opt.has_value());
    EXPECT_TRUE(node.IsRoot());
  }

  //! Validates transform values for a node.
  static void ExpectTransformValues(const SceneNode& node,
      const glm::vec3& expected_position, const glm::vec3& expected_scale)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    const auto& transform
        = impl_opt->get().GetComponent<scene::detail::TransformComponent>();
    EXPECT_EQ(transform.GetLocalPosition(), expected_position);
    EXPECT_EQ(transform.GetLocalScale(), expected_scale);
  }

  //=== Transform Helpers ===-------------------------------------------------//

  //! Sets up transform with specific values.
  void SetupNodeTransform(const SceneNode& node,
      const scene::detail::TransformComponent::Vec3& position,
      const scene::detail::TransformComponent::Quat& rotation,
      const scene::detail::TransformComponent::Vec3& scale) const
  {
    auto node_impl_opt = node.GetObject();
    ASSERT_TRUE(node_impl_opt.has_value());

    auto& transform = node_impl_opt->get()
                          .GetComponent<scene::detail::TransformComponent>();
    transform.SetLocalTransform(position, rotation, scale);
  }

  //! Gets transform component from node.
  auto GetTransformComponent(const SceneNode& node) const
      -> scene::detail::TransformComponent&
  {
    auto node_impl_opt = node.GetObject();
    EXPECT_TRUE(node_impl_opt.has_value())
        << "Node should have valid implementation";
    return node_impl_opt->get()
        .GetComponent<scene::detail::TransformComponent>();
  }

  //! Updates scene transforms to ensure cached world values are valid.
  void UpdateSceneTransforms() const
  {
    scene_->Update(false); // Update transforms without skipping dirty flags
  }

  //! Creates a node with a specific position.
  [[nodiscard]] auto CreateNodeWithPosition(
      const std::string& name, const glm::vec3& position) const -> SceneNode
  {
    auto node = CreateNode(name);
    SetNodePosition(node, position);
    return node;
  }

  //! Sets the local position of a node.
  static void SetNodePosition(SceneNode& node, const glm::vec3& position)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    auto& transform
        = impl_opt->get().GetComponent<scene::detail::TransformComponent>();
    transform.SetLocalPosition(position);
  }

  //! Sets the local scale of a node.
  static void SetNodeScale(SceneNode& node, const glm::vec3& scale)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    auto& transform
        = impl_opt->get().GetComponent<scene::detail::TransformComponent>();
    transform.SetLocalScale(scale);
  }

  //! Sets both position and scale for a node.
  static void SetNodeTransformValues(
      SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    auto& transform
        = impl_opt->get().GetComponent<scene::detail::TransformComponent>();
    transform.SetLocalPosition(position);
    transform.SetLocalScale(scale);
  }

  //! Updates transforms for a single node (clears dirty flags).
  void UpdateSingleNodeTransforms(SceneNode& node) const
  {
    const auto impl = node.GetObject();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(*scene_);
  }

  //=== Common Scene Setups ==================================================//

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

  //! Three-level hierarchy: Grandparent -> Parent -> Child
  struct ThreeLevelHierarchy {
    SceneNode grandparent;
    SceneNode parent;
    SceneNode child;
  };

  //! Creates a three-level hierarchy.
  [[nodiscard]] auto CreateThreeLevelHierarchy(
      const std::string& grandparent_name = "Grandparent",
      const std::string& parent_name = "Parent",
      const std::string& child_name = "Child") const -> ThreeLevelHierarchy
  {
    auto grandparent = CreateNode(grandparent_name);
    auto parent_opt = CreateChildNode(grandparent, parent_name);
    EXPECT_TRUE(parent_opt.has_value());
    auto child_opt = CreateChildNode(parent_opt.value(), child_name);
    EXPECT_TRUE(child_opt.has_value());
    return { std::move(grandparent), std::move(parent_opt.value()),
      std::move(child_opt.value()) };
  }

  //! Complex hierarchy with mixed visibility: Root with visible and invisible
  //! children
  struct MixedVisibilityHierarchy {
    SceneNode root;
    SceneNode visible_child;
    SceneNode invisible_child;
    SceneNode visible_grandchild;
  };

  //! Creates a hierarchy with mixed visibility settings.
  [[nodiscard]] auto CreateMixedVisibilityHierarchy() const
      -> MixedVisibilityHierarchy
  {
    auto root = CreateVisibleNode("Root");
    auto visible_child_opt = CreateVisibleChildNode(root, "VisibleChild", true);
    auto invisible_child_opt
        = CreateVisibleChildNode(root, "InvisibleChild", false);
    EXPECT_TRUE(visible_child_opt.has_value());
    EXPECT_TRUE(invisible_child_opt.has_value());

    auto visible_grandchild_opt = CreateVisibleChildNode(
        visible_child_opt.value(), "VisibleGrandchild", true);
    EXPECT_TRUE(visible_grandchild_opt.has_value());

    return { std::move(root), std::move(visible_child_opt.value()),
      std::move(invisible_child_opt.value()),
      std::move(visible_grandchild_opt.value()) };
  }

  //=== Error Testing Helpers ===--------------------------------------------//

  //! Creates multiple nodes and validates they all have unique handles.
  void ValidateUniqueHandles() const
  {
    auto node1 = CreateNode("Node1");
    auto node2 = CreateNode("Node2");
    auto node3 = CreateNode("Node3");
    ExpectHandlesUnique(node1, node2, node3);
  }

  //! Tests various special character combinations in node names.
  void TestSpecialCharacterNames() const
  {
    const std::vector<std::string> special_names = {
      "Node@#$%", "Node With Spaces", "Node\tWith\nSpecial\rChars",
      "Node_with-symbols.123", "üñîçødé",
      "", // Empty name
      std::string(100, 'A') // Very long name
    };

    for (const auto& name : special_names) {
      auto node = CreateNode(name);
      CHECK_FOR_FAILURES_MSG(
          ExpectNodeValidWithName(node, name), "TestSpecialCharacterNames");
    }
  }
};

//=== Categorized Test Fixtures ===----------------------------------------//

/// Base class for basic functionality tests.
class SceneBasicTest : public SceneTest { };

/// Base class for error condition tests.
class SceneErrorTest : public SceneTest { };

/// Base class for death tests (for assertion failures).
class SceneDeathTest : public SceneTest { };

/// Base class for edge case tests.
class SceneEdgeCaseTest : public SceneTest { };

/// Base class for performance/stress tests.
class ScenePerformanceTest : public SceneTest {
protected:
  void SetUp() override
  {
    // Use larger scene capacity for performance tests
    scene_ = std::make_shared<Scene>("PerformanceTestScene", 4096);
  }
};

/// Base class for functional integration tests.
class SceneFunctionalTest : public SceneTest { };

} // namespace oxygen::scene::testing
