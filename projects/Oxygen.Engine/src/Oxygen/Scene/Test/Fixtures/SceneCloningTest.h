//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Test/Fixtures/SceneTest.h>
#include <functional>
#include <unordered_map>

namespace oxygen::scene::testing {

//=============================================================================
// Scene Cloning and Serialization Test Infrastructure
//=============================================================================

/// Base fixture for scene cloning and serialization tests.
class SceneCloningTest : public SceneTest {
protected:
  void SetUp() override
  {
    SceneTest::SetUp();
    // Create a destination scene for cloning operations
    dest_scene_ = std::make_shared<Scene>("DestinationScene", 1024);
  }

  void TearDown() override
  {
    dest_scene_.reset();
    SceneTest::TearDown();
  }

  //=== Cloning Infrastructure ===--------------------------------------------//

  std::shared_ptr<Scene> dest_scene_;

  //=== Cloning Assertion Helpers ============================================//

  /// Validates that two nodes have equivalent content but different handles.
  static void ExpectNodesEquivalent(
      const SceneNode& original, const SceneNode& cloned)
  {
    // Should have different handles (different scenes/instances)
    EXPECT_NE(original.GetHandle(), cloned.GetHandle())
        << "Cloned node should have different handle";

    // Should have equivalent names and properties
    const auto orig_obj_opt = original.GetObject();
    const auto cloned_obj_opt = cloned.GetObject();

    ASSERT_TRUE(orig_obj_opt.has_value())
        << "Original node should have valid object";
    ASSERT_TRUE(cloned_obj_opt.has_value())
        << "Cloned node should have valid object";

    EXPECT_EQ(orig_obj_opt->get().GetName(), cloned_obj_opt->get().GetName())
        << "Cloned node should have same name";

    // Compare transforms
    const auto& orig_transform
        = orig_obj_opt->get().GetComponent<scene::detail::TransformComponent>();
    const auto& cloned_transform
        = cloned_obj_opt->get()
              .GetComponent<scene::detail::TransformComponent>();

    EXPECT_EQ(
        orig_transform.GetLocalPosition(), cloned_transform.GetLocalPosition())
        << "Cloned node should have same position";
    EXPECT_EQ(
        orig_transform.GetLocalRotation(), cloned_transform.GetLocalRotation())
        << "Cloned node should have same rotation";
    EXPECT_EQ(orig_transform.GetLocalScale(), cloned_transform.GetLocalScale())
        << "Cloned node should have same scale";
  }

  /// Validates that a hierarchy has been cloned correctly.
  void ExpectHierarchyClonedCorrectly(
      const SceneNode& original_root, const SceneNode& cloned_root)
  {
    // Validate root nodes
    ExpectNodesEquivalent(original_root, cloned_root);

    // Build maps of original and cloned hierarchies
    std::unordered_map<std::string, SceneNode> original_nodes;
    std::unordered_map<std::string, SceneNode> cloned_nodes;

    CollectHierarchyNodes(original_root, original_nodes);
    CollectHierarchyNodes(cloned_root, cloned_nodes);

    // Should have same number of nodes
    EXPECT_EQ(original_nodes.size(), cloned_nodes.size())
        << "Cloned hierarchy should have same number of nodes";

    // Validate each node pair
    for (const auto& [name, original_node] : original_nodes) {
      auto cloned_it = cloned_nodes.find(name);
      ASSERT_NE(cloned_it, cloned_nodes.end())
          << "Cloned hierarchy should contain node: " << name;

      ExpectNodesEquivalent(original_node, cloned_it->second);

      // Validate parent-child relationships
      ExpectParentChildRelationshipsMatch(
          original_node, cloned_it->second, original_nodes, cloned_nodes);
    }
  }

  /// Validates that cloned scenes have equivalent structure.
  void ExpectScenesEquivalent(const Scene& original, const Scene& cloned)
  {
    EXPECT_EQ(original.GetNodeCount(), cloned.GetNodeCount())
        << "Cloned scene should have same node count";

    // Note: Names might be different for cloned scenes
    // Focus on structural equivalence rather than exact name matching
  }

  //=== Cloning Helper Methods ===--------------------------------------------//

  /// Recursively collects all nodes in a hierarchy.
  static void CollectHierarchyNodes(const SceneNode& root,
      std::unordered_map<std::string, SceneNode>& node_map)
  {
    const auto obj_opt = root.GetObject();
    if (!obj_opt.has_value()) {
      return;
    }

    const std::string name = std::string(obj_opt->get().GetName());
    node_map[name] = root;

    // Recursively collect children
    auto child_opt = root.GetFirstChild();
    while (child_opt.has_value()) {
      CollectHierarchyNodes(child_opt.value(), node_map);
      child_opt = child_opt->GetNextSibling();
    }
  }

  /// Validates that parent-child relationships match between original and
  /// cloned hierarchies.
  static void ExpectParentChildRelationshipsMatch(
      const SceneNode& original_node, const SceneNode& cloned_node,
      const std::unordered_map<std::string, SceneNode>& original_nodes,
      const std::unordered_map<std::string, SceneNode>& cloned_nodes)
  {
    // Check parent relationships
    const auto orig_parent_opt = original_node.GetParent();
    const auto cloned_parent_opt = cloned_node.GetParent();

    if (orig_parent_opt.has_value()) {
      ASSERT_TRUE(cloned_parent_opt.has_value())
          << "Cloned node should have parent if original has parent";

      const auto orig_parent_obj = orig_parent_opt->GetObject();
      const auto cloned_parent_obj = cloned_parent_opt->GetObject();

      if (orig_parent_obj.has_value() && cloned_parent_obj.has_value()) {
        EXPECT_EQ(orig_parent_obj->get().GetName(),
            cloned_parent_obj->get().GetName())
            << "Cloned node should have parent with same name";
      }
    } else {
      EXPECT_FALSE(cloned_parent_opt.has_value())
          << "Cloned node should not have parent if original doesn't have "
             "parent";
    }

    // Check child count
    std::size_t orig_child_count = 0;
    std::size_t cloned_child_count = 0;

    auto orig_child = original_node.GetFirstChild();
    while (orig_child.has_value()) {
      ++orig_child_count;
      orig_child = orig_child->GetNextSibling();
    }

    auto cloned_child = cloned_node.GetFirstChild();
    while (cloned_child.has_value()) {
      ++cloned_child_count;
      cloned_child = cloned_child->GetNextSibling();
    }

    EXPECT_EQ(orig_child_count, cloned_child_count)
        << "Cloned node should have same number of children";
  }

  //=== Common Cloning Scenarios ===------------------------------------------//

  /// Creates a simple hierarchy for cloning tests.
  [[nodiscard]] auto CreateSimpleCloningHierarchy() const -> SimpleParentChild
  {
    auto setup = CreateSimpleParentChild();

    // Add some transform data to make cloning more interesting
    SetTransformValues(
        setup.parent, { 1.0f, 2.0f, 3.0f }, { 0.5f, 1.0f, 1.5f });
    SetTransformValues(
        setup.child, { -1.0f, 0.0f, 1.0f }, { 2.0f, 2.0f, 2.0f });

    return setup;
  }

  /// Creates a complex hierarchy for comprehensive cloning tests.
  [[nodiscard]] auto CreateComplexCloningHierarchy() const -> ThreeLevelSetup
  {
    auto setup = CreateThreeLevelHierarchy(
        "CloneGrandparent", "CloneParent", "CloneChild");

    // Set different transforms for each level
    SetTransformValues(
        setup.grandparent, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    SetTransformValues(
        setup.parent, { 5.0f, 0.0f, 0.0f }, { 0.8f, 0.8f, 0.8f });
    SetTransformValues(setup.child, { 0.0f, 3.0f, 0.0f }, { 1.2f, 1.2f, 1.2f });

    return setup;
  }

  /// Creates a wide hierarchy (many siblings) for cloning stress tests.
  struct WideCloningSetup {
    SceneNode root;
    std::vector<SceneNode> children;
  };

  /// Creates a wide hierarchy for cloning tests.
  [[nodiscard]] auto CreateWideCloningHierarchy(std::size_t num_children
      = 5) const -> WideCloningSetup
  {
    auto root = CreateNode("WideRoot");
    std::vector<SceneNode> children;
    children.reserve(num_children);

    for (std::size_t i = 0; i < num_children; ++i) {
      auto child_name = "WideChild_" + std::to_string(i);
      auto child_opt = CreateChildNode(root, child_name);
      EXPECT_TRUE(child_opt.has_value());

      // Set unique transform for each child
      const float offset = static_cast<float>(i);
      SetTransformValues(child_opt.value(), { offset, 0.0f, 0.0f },
          { 1.0f + offset * 0.1f, 1.0f, 1.0f });

      children.push_back(std::move(child_opt.value()));
    }

    return { std::move(root), std::move(children) };
  }

  //=== Serialization Testing Helpers ===-------------------------------------//

  /// Placeholder for future serialization validation.
  void ValidateSerializedData(const std::string& /*serialized_data*/) const
  {
    // Future implementation for serialization validation
    // Could include JSON schema validation, binary format checks, etc.
  }

  /// Placeholder for round-trip serialization testing.
  void TestSerializationRoundTrip(const SceneNode& /*original*/) const {
    // Future implementation for serialize -> deserialize -> compare workflow  }
  };
};

//=== Categorized Cloning Test Fixtures ===--------------------------------//

/// Base class for basic cloning functionality tests.
class SceneCloningBasicTest : public SceneCloningTest { };

/// Base class for deep cloning tests.
class SceneCloningDeepTest : public SceneCloningTest { };

/// Base class for cloning performance tests.
class SceneCloningPerformanceTest : public SceneCloningTest {
protected:
  void SetUp() override
  {
    // Use larger scenes for performance testing
    scene_ = std::make_shared<Scene>("PerformanceSourceScene", 4096);
    dest_scene_ = std::make_shared<Scene>("PerformanceDestinationScene", 4096);
  }
};

/// Base class for serialization tests.
class SceneSerializationTest : public SceneCloningTest { };

/// Base class for cross-scene cloning tests.
class SceneCrossSceneCloningTest : public SceneCloningTest { };

} // namespace oxygen::scene::testing
