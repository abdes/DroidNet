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

// Aliases for gtest/gmock matchers to reduce clutter
using testing::Contains;
using testing::Not;
using testing::UnorderedElementsAreArray;

namespace oxygen::scene::testing {

//=============================================================================
// Base Traversal Test Fixture
//=============================================================================

class SceneTraversalTestBase : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    scene_ = std::make_shared<Scene>("TraversalTestScene", 1024);
  }

  auto TearDown() -> void override
  {
    scene_.reset();
    visited_nodes_.clear();
    visit_order_.clear();
  }

  // Helper method to get traversal when needed
  auto GetTraversal() -> Scene::MutatingTraversal { return scene_->Traverse(); }

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
    SceneNode& parent, const std::string& name) const -> SceneNode
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
    SceneNode& parent, const std::string& name) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}.SetFlag(
      SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
    auto child_opt = scene_->CreateChildNode(parent, name, flags);
    EXPECT_TRUE(child_opt.has_value());
    return child_opt.value();
  }

  // Helper: Clear a node's dirty transform flag
  auto UpdateSingleNodeTransforms(SceneNode& node) const -> void
  {
    const auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    // Remove const to call the non-const method
    impl->get().UpdateTransforms(*scene_);
  }

  // Helper: Visit tracking visitor
  auto CreateTrackingVisitor()
  {
    auto v
      = [this](const MutableVisitedNode& node, bool dry_run) -> VisitResult {
      if (!dry_run) {
        visited_nodes_.push_back(node.node_impl);
        visit_order_.emplace_back(
          node.node_impl->GetName()); // Convert string_view to string
      }
      return VisitResult::kContinue;
    };
    static_assert(oxygen::scene::MutatingSceneVisitor<decltype(v)>);
    return v;
  }

  // Helper: Visit tracking visitor with early termination
  auto CreateEarlyTerminationVisitor(const std::string& stop_at_name)
  {
    return [this, stop_at_name](const auto& node, bool dry_run) -> VisitResult {
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
    return
      [this, skip_subtree_of](const auto& node, bool dry_run) -> VisitResult {
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
    return [reject_names](auto& visited_node,
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
    return [reject_subtree_names](const auto& visited_node,
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
  auto ExpectVisitedNodes(const std::vector<std::string>& expected_names) const
    -> void
  {
    ASSERT_EQ(visit_order_.size(), expected_names.size());
    for (size_t i = 0; i < expected_names.size(); ++i) {
      EXPECT_EQ(visit_order_[i], expected_names[i])
        << "Mismatch at position " << i;
    }
  }

  static auto ExpectTraversalResult(const TraversalResult& result,
    const std::size_t expected_visited, const std::size_t expected_filtered,
    const bool expected_completed = true) -> void
  {
    EXPECT_EQ(result.nodes_visited, expected_visited);
    EXPECT_EQ(result.nodes_filtered, expected_filtered);
    EXPECT_EQ(result.completed, expected_completed);
  }

  // Helper: Verify all expected nodes are present (order-independent)
  auto ExpectContainsAllNodes(
    const std::vector<std::string>& expected_nodes) const -> void
  {
    EXPECT_THAT(visit_order_, UnorderedElementsAreArray(expected_nodes));
  }

  // Helper: Verify none of the forbidden nodes are present
  auto ExpectContainsNoForbiddenNodes(
    const std::vector<std::string>& forbidden_nodes) const -> void
  {
    for (const auto& forbidden : forbidden_nodes) {
      EXPECT_THAT(visit_order_, Not(Contains(forbidden)))
        << "Found forbidden node (should not be present): " << forbidden;
    }
  }

  // Helper: Verify expected nodes are present and forbidden nodes are not
  auto ExpectContainsExactlyNodes(
    const std::vector<std::string>& expected_nodes,
    const std::vector<std::string>& forbidden_nodes = {}) const -> void
  {
    EXPECT_THAT(visit_order_, UnorderedElementsAreArray(expected_nodes));
    for (const auto& forbidden : forbidden_nodes) {
      EXPECT_THAT(visit_order_, Not(Contains(forbidden)))
        << "Found forbidden node (should not be present): " << forbidden;
    }
    EXPECT_EQ(visit_order_.size(), expected_nodes.size())
      << "Should visit exactly " << expected_nodes.size() << " nodes";
  }

  std::shared_ptr<Scene> scene_;
  std::vector<SceneNodeImpl*> visited_nodes_;
  std::vector<std::string> visit_order_;
};

class SceneTraversalBasicTest : public SceneTraversalTestBase {
protected:
  auto SetUp() -> void override
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

  [[nodiscard]] auto GetNodeCount() const -> std::size_t { return 6; }

  // Helper: Verify complete semantic ordering for the basic test hierarchy
  auto ExpectSemanticOrdering(TraversalOrder order) const -> void
  {
    switch (order) {
    case TraversalOrder::kPreOrder:
      // Pre-order: parent before children
      TRACE_GCHECK_F(
        ExpectContainsExactlyNodes({ "root", "A", "C", "D", "B", "E" }),
        "pre-order");
      break;
    case TraversalOrder::kPostOrder:
      // Post-order: children before parent
      TRACE_GCHECK_F(
        ExpectContainsExactlyNodes({ "C", "D", "A", "E", "B", "root" }),
        "post-order");
      break;
    case TraversalOrder::kBreadthFirst:
      // Breadth-first: level by level
      TRACE_GCHECK_F(
        ExpectContainsExactlyNodes({ "root", "A", "B", "C", "D", "E" }),
        "breadth-first");
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
