//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene::testing {

//=============================================================================
// Base Traversal Test Fixture
//=============================================================================

class SceneTraversalTestBase : public ::testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("TraversalTestScene", 1024);
    traversal_ = std::make_unique<SceneTraversal>(*scene_);
  }

  void TearDown() override
  {
    traversal_.reset();
    scene_.reset();
    visited_nodes_.clear();
    visit_order_.clear();
  }

  // Helper: Create a scene node with proper flags
  [[nodiscard]] auto CreateNode(const std::string& name,
    const glm::vec3& position = { 0.0f, 0.0f, 0.0f }) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kStatic,
                           SceneFlag {}.SetEffectiveValueBit(false));
    auto node = scene_->CreateNode(name, flags);
    EXPECT_TRUE(node.IsValid());

    // Set transform if not default
    if (position != glm::vec3 { 0.0f, 0.0f, 0.0f }) {
      auto transform = node.GetTransform();
      transform.SetLocalPosition(position);
    }

    return node;
  }

  // Helper: Create child node
  [[nodiscard]] auto CreateChildNode(
    const SceneNode& parent, const std::string& name) const -> SceneNode
  {
    auto child_opt = scene_->CreateChildNode(parent, name);
    EXPECT_TRUE(child_opt.has_value());
    return child_opt.value();
  }

  [[nodiscard]] auto CreateInvisibleNode(const std::string& name) const
    -> SceneNode
  {
    const auto flags = SceneNode::Flags {}.SetFlag(
      SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
    auto node = scene_->CreateNode(name, flags);
    EXPECT_TRUE(node.IsValid());
    return node;
  }

  // Helper: Create invisible child node
  [[nodiscard]] auto CreateInvisibleChildNode(
    const SceneNode& parent, const std::string& name) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}.SetFlag(
      SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
    auto child_opt = scene_->CreateChildNode(parent, name, flags);
    EXPECT_TRUE(child_opt.has_value());
    return child_opt.value();
  }

  // Helper: Mark a node's transform as dirty
  static void MarkNodeTransformDirty(SceneNode& node)
  {
    const auto impl = node.GetObject();
    ASSERT_TRUE(impl.has_value());
    if (const auto pos = node.GetTransform().GetLocalPosition()) {
      node.GetTransform().SetLocalPosition(
        *pos + glm::vec3(0.001f, 0.0f, 0.0f));
    }
    // Also mark the node itself as transform dirty
    impl->get().MarkTransformDirty();
  }

  // Helper: Check if a node's transform is dirty
  static auto IsNodeTransformDirty(const SceneNode& node) -> bool
  {
    const auto impl = node.GetObject();
    if (!impl.has_value())
      return false;
    return impl->get().IsTransformDirty();
  }

  // Helper: Clear a node's dirty transform flag
  void UpdateSingleNodeTransforms(SceneNode& node) const
  {
    const auto impl = node.GetObject();
    ASSERT_TRUE(impl.has_value());
    // Remove const to call the non-const method
    impl->get().UpdateTransforms(*scene_);
  }
  // Helper: Visit tracking visitor
  auto CreateTrackingVisitor()
  {
    return [this](const VisitedNode& node, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(
          node.node_impl->GetName()); // Convert string_view to string
      }
      return VisitResult::kContinue;
    };
  }

  // Helper: Visit tracking visitor with early termination
  auto CreateEarlyTerminationVisitor(const std::string& stop_at_name)
  {
    return [this, stop_at_name](const VisitedNode& node, const Scene& /*scene*/,
             bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(
          node.node_impl->GetName()); // Convert string_view to string
      }
      return node.node_impl->GetName() == stop_at_name ? VisitResult::kStop
                                                       : VisitResult::kContinue;
    };
  }

  // Helper: Visit tracking visitor with subtree skipping
  auto CreateSubtreeSkippingVisitor(const std::string& skip_subtree_of)
  {
    return [this, skip_subtree_of](const VisitedNode& node,
             const Scene& /*scene*/, bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(
          node.node_impl->GetName()); // Convert string_view to string
      }
      return node.node_impl->GetName() == skip_subtree_of
        ? VisitResult::kSkipSubtree
        : VisitResult::kContinue;
    };
  }

  // Helper: Create a filter that rejects specific nodes
  static auto CreateRejectFilter(const std::vector<std::string>& reject_names)
  {
    return [reject_names](const VisitedNode& visited_node,
             FilterResult /*parent_filter_result*/) -> FilterResult {
      if (visited_node.node_impl == nullptr) {
        return FilterResult::kReject; // Safety check for null pointer
      }
      for (const auto& name : reject_names) {
        if (visited_node.node_impl->GetName() == name) {
          return FilterResult::kReject;
        }
      }
      return FilterResult::kAccept;
    };
  }

  // Helper: Create a filter that rejects subtrees of specific nodes
  static auto CreateRejectSubtreeFilter(
    const std::vector<std::string>& reject_subtree_names)
  {
    return [reject_subtree_names](const VisitedNode& visited_node,
             FilterResult /*parent_filter_result*/) -> FilterResult {
      for (const auto& name : reject_subtree_names) {
        if (visited_node.node_impl->GetName() == name) {
          return FilterResult::kRejectSubTree;
        }
      }
      return FilterResult::kAccept;
    };
  }

  // Expectation helpers
  void ExpectVisitedNodes(const std::vector<std::string>& expected_names) const
  {
    ASSERT_EQ(visit_order_.size(), expected_names.size());
    for (size_t i = 0; i < expected_names.size(); ++i) {
      EXPECT_EQ(visit_order_[i], expected_names[i])
        << "Mismatch at position " << i;
    }
  }

  static void ExpectTraversalResult(const TraversalResult& result,
    const std::size_t expected_visited, const std::size_t expected_filtered,
    const bool expected_completed = true)
  {
    EXPECT_EQ(result.nodes_visited, expected_visited);
    EXPECT_EQ(result.nodes_filtered, expected_filtered);
    EXPECT_EQ(result.completed, expected_completed);
  }

  // Helper: Verify all expected nodes are present (order-independent)
  void ExpectContainsAllNodes(
    const std::vector<std::string>& expected_nodes) const
  {
    using ::testing::Contains;

    for (const auto& expected : expected_nodes) {
      EXPECT_THAT(visit_order_, Contains(expected))
        << "Missing expected node: " << expected;
    }
  }

  // Helper: Verify none of the forbidden nodes are present
  void ExpectContainsNoForbiddenNodes(
    const std::vector<std::string>& forbidden_nodes) const
  {
    using ::testing::Contains;
    using ::testing::Not;

    for (const auto& forbidden : forbidden_nodes) {
      EXPECT_THAT(visit_order_, Not(Contains(forbidden)))
        << "Found forbidden node (should not be present): " << forbidden;
    }
  }

  // Helper: Verify expected nodes are present and forbidden nodes are not
  void ExpectContainsExactlyNodes(
    const std::vector<std::string>& expected_nodes,
    const std::vector<std::string>& forbidden_nodes = {}) const
  {
    ExpectContainsAllNodes(expected_nodes);
    ExpectContainsNoForbiddenNodes(forbidden_nodes);
    EXPECT_EQ(visit_order_.size(), expected_nodes.size())
      << "Should visit exactly " << expected_nodes.size() << " nodes";
  } // Helper: Verify level-based ordering for breadth-first traversal
  void ExpectLevelBasedOrdering(const std::vector<std::string>& level1_nodes,
    const std::vector<std::string>& level2_nodes) const
  {
    auto find_pos = [this](const std::string& name) {
      return std::ranges::find(visit_order_, name) - visit_order_.begin();
    };

    // Find max position of level 1 and min position of level 2
    size_t max_level1_pos = 0;
    for (const auto& node : level1_nodes) {
      max_level1_pos
        = std::max(max_level1_pos, static_cast<size_t>(find_pos(node)));
    }

    size_t min_level2_pos = visit_order_.size();
    for (const auto& node : level2_nodes) {
      min_level2_pos
        = std::min(min_level2_pos, static_cast<size_t>(find_pos(node)));
    }

    EXPECT_LT(max_level1_pos, min_level2_pos)
      << "Level 1 nodes should come before level 2 nodes in "
         "breadth-first traversal";
  }

  // Helper: Verify parent-child ordering semantics
  void ExpectParentBeforeChild(
    const std::string& parent, const std::string& child) const
  {
    auto find_pos = [this](const std::string& name) {
      return std::ranges::find(visit_order_, name) - visit_order_.begin();
    };

    const auto parent_pos = find_pos(parent);
    const auto child_pos = find_pos(child);

    EXPECT_LT(parent_pos, child_pos)
      << "Parent '" << parent << "' should be visited before child '" << child
      << "'";
  }

  // Helper: Verify child-parent ordering semantics (for post-order)
  void ExpectChildBeforeParent(
    const std::string& child, const std::string& parent) const
  {
    auto find_pos = [this](const std::string& name) {
      return std::ranges::find(visit_order_, name) - visit_order_.begin();
    };

    const auto child_pos = find_pos(child);
    const auto parent_pos = find_pos(parent);

    EXPECT_LT(child_pos, parent_pos)
      << "Child '" << child << "' should be visited before parent '" << parent
      << "'";
  }

  std::shared_ptr<Scene> scene_;
  std::unique_ptr<SceneTraversal> traversal_;
  std::vector<SceneNodeImpl*> visited_nodes_;
  std::vector<std::string> visit_order_;
};

class SceneTraversalBasicTest : public SceneTraversalTestBase {
protected:
  void SetUp() override
  {
    SceneTraversalTestBase::SetUp();
    // Create a simple test hierarchy:
    //     root
    //    /    \
        //   A      B
    //  / \    /
    // C   D  E
    root_ = CreateNode("root");
    nodeA_ = CreateChildNode(root_, "A");
    nodeB_ = CreateChildNode(root_, "B");
    nodeC_ = CreateChildNode(nodeA_, "C");
    nodeD_ = CreateChildNode(nodeA_, "D");
    nodeE_ = CreateChildNode(nodeB_, "E");

    // As a clean start, update the transforms of all nodes.
    UpdateSingleNodeTransforms(root_);
    UpdateSingleNodeTransforms(nodeA_);
    UpdateSingleNodeTransforms(nodeB_);
    UpdateSingleNodeTransforms(nodeC_);
    UpdateSingleNodeTransforms(nodeD_);
    UpdateSingleNodeTransforms(nodeE_);
  }

  // Helper: Verify complete semantic ordering for the basic test hierarchy
  void ExpectSemanticOrdering(TraversalOrder order) const
  {
    switch (order) {
    case TraversalOrder::kPreOrder:
      // Pre-order: parent before children
      ExpectParentBeforeChild("root", "A");
      ExpectParentBeforeChild("root", "B");
      ExpectParentBeforeChild("A", "C");
      ExpectParentBeforeChild("A", "D");
      ExpectParentBeforeChild("B", "E");
      break;

    case TraversalOrder::kPostOrder:
      // Post-order: children before parent
      ExpectChildBeforeParent("A", "root");
      ExpectChildBeforeParent("B", "root");
      ExpectChildBeforeParent("C", "A");
      ExpectChildBeforeParent("D", "A");
      ExpectChildBeforeParent("E", "B");
      break;

    case TraversalOrder::kBreadthFirst:
      // Breadth-first: level by level
      ExpectLevelBasedOrdering({ "A", "B" }, { "C", "D", "E" });
      break;
    }
  }

  // Member variables using default constructor for SceneNode
  SceneNode root_;
  SceneNode nodeA_;
  SceneNode nodeB_;
  SceneNode nodeC_;
  SceneNode nodeD_;
  SceneNode nodeE_;
};

} // namespace oxygen::scene::testing
