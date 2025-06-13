//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "./Scene_traversal_test.h"

using oxygen::scene::AcceptAllFilter;
using oxygen::scene::ConstVisitedNode;
using oxygen::scene::DirtyTransformFilter;
using oxygen::scene::FilterResult;
using oxygen::scene::MutableVisitedNode;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::SceneTraversal;
using oxygen::scene::TraversalOrder;
using oxygen::scene::TraversalResult;
using oxygen::scene::VisibleFilter;
using oxygen::scene::VisitResult;

using oxygen::scene::testing::SceneTraversalBasicTest;
using oxygen::scene::testing::SceneTraversalTestBase;

namespace {

//=============================================================================
// Traversal From Specific Roots Tests
//=============================================================================

class SceneTraversalFromRootsTest : public SceneTraversalTestBase {
protected:
  void SetUp() override
  {
    SceneTraversalTestBase::SetUp();
    // Create multiple root hierarchies:
    //   root1     root2     root3
    //   /  \        |       (leaf)
    //  A    B       C
    //       |
    //       D
    root1_ = CreateNode("root1");
    root2_ = CreateNode("root2");
    root3_ = CreateNode("root3");
    nodeA_ = CreateChildNode(root1_, "A");
    nodeB_ = CreateChildNode(root1_, "B");
    nodeC_ = CreateChildNode(root2_, "C");
    nodeD_ = CreateChildNode(nodeB_, "D");

    // As a clean start, update the transforms of all nodes.
    UpdateSingleNodeTransforms(root1_);
    UpdateSingleNodeTransforms(root2_);
    UpdateSingleNodeTransforms(root3_);
    UpdateSingleNodeTransforms(nodeA_);
    UpdateSingleNodeTransforms(nodeB_);
    UpdateSingleNodeTransforms(nodeC_);
    UpdateSingleNodeTransforms(nodeD_);
  }

  // Member variables using default constructor for SceneNode
  SceneNode root1_;
  SceneNode root2_;
  SceneNode root3_;
  SceneNode nodeA_;
  SceneNode nodeB_;
  SceneNode nodeC_;
  SceneNode nodeD_;
};

//! Tests that traversing from a single root only visits that root's hierarchy.
NOLINT_TEST_F(SceneTraversalFromRootsTest, TraverseFromSingleRoot)
{
  // Act: Traverse from root1 only
  const auto result
    = GetTraversal().TraverseHierarchy(root1_, CreateTrackingVisitor());

  // Assert: Only root1's hierarchy should be visited
  ExpectTraversalResult(result, 4, 0, true);
  ExpectContainsExactlyNodes({ "root1", "A", "B", "D" });
}

//! Tests that traversing from multiple roots only visits the specified
//! subtrees.
NOLINT_TEST_F(SceneTraversalFromRootsTest, TraverseFromMultipleRoots)
{
  // Arrange: Prepare handles for root1 and root3
  std::vector roots = { root1_, root3_ };

  // Act: Traverse from multiple specific roots
  const auto result
    = GetTraversal().TraverseHierarchies(roots, CreateTrackingVisitor());

  // Assert: Only specified roots' hierarchies should be visited
  ExpectTraversalResult(result, 5, 0, true);
  ExpectContainsExactlyNodes({ "root1", "A", "B", "D", "root3" });
}

//! Tests that traversing from an empty root list visits no nodes.
NOLINT_TEST_F(SceneTraversalFromRootsTest, TraverseFromEmptyRootList)
{
  // Arrange: Empty root list
  std::vector<SceneNode> empty_roots;

  // Act: Traverse from empty root list
  const auto result
    = GetTraversal().TraverseHierarchies(empty_roots, CreateTrackingVisitor());

  // Assert: No nodes should be visited
  ExpectTraversalResult(result, 0, 0, true);
  EXPECT_TRUE(visit_order_.empty());
}

//! Tests that traversing from an invalid handle visits no nodes.
NOLINT_TEST_F(SceneTraversalFromRootsTest, TraverseFromInvalidHandle)
{
  // Arrange: Invalid node using default constructor
  auto invalid_node = SceneNode();

  // Act: Traverse from invalid handle
  const auto result
    = GetTraversal().TraverseHierarchy(invalid_node, CreateTrackingVisitor());

  // Assert: No nodes should be visited
  ExpectTraversalResult(result, 0, 0, true);
  EXPECT_TRUE(visit_order_.empty());
}

//=============================================================================
// Transform Update Tests
//=============================================================================

class SceneTraversalTransformTest : public SceneTraversalTestBase {
protected:
  void SetUp() override
  {
    SceneTraversalTestBase::SetUp();
    // Create test hierarchy - nodes are created clean by helper methods
    root_ = CreateNode("root");
    nodeA_ = CreateChildNode(root_, "A");
    nodeB_ = CreateChildNode(root_, "B");
    nodeC_ = CreateChildNode(nodeA_, "C");

    // As a clean start, update the transforms of all nodes.
    UpdateSingleNodeTransforms(root_);
    UpdateSingleNodeTransforms(nodeA_);
    UpdateSingleNodeTransforms(nodeB_);
    UpdateSingleNodeTransforms(nodeC_);
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
  static auto IsNodeTransformDirty(SceneNode& node) -> bool
  {
    const auto impl = node.GetObject();
    if (!impl.has_value())
      return false;
    return impl->get().IsTransformDirty();
  }

  // Member variables using default constructor for SceneNode
  SceneNode root_;
  SceneNode nodeA_;
  SceneNode nodeB_;
  SceneNode nodeC_;
};

//! Tests that only dirty nodes are visited when using DirtyTransformFilter.
NOLINT_TEST_F(SceneTraversalTransformTest, DirtyTransformFilter)
{
  // Arrange: Mark specific nodes as dirty
  MarkNodeTransformDirty(nodeA_);
  MarkNodeTransformDirty(nodeC_);

  // Act: Traverse with dirty transform filter
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor(),
    TraversalOrder::kPreOrder, DirtyTransformFilter {});

  // Assert: A, and C are visited; only B is filtered out
  ExpectTraversalResult(result, 2, 2, true);
  ExpectContainsExactlyNodes({ "A", "C" });
}

//! Tests that the UpdateTransforms method updates all dirty sub-trees, unless a
//! node explicitly ignores its parent transform.
NOLINT_TEST_F(SceneTraversalTransformTest, UpdateTransformsMethod)
{
  // Arrange: Mark specific nodes as dirty
  MarkNodeTransformDirty(nodeA_);
  MarkNodeTransformDirty(nodeB_);
  // C will ignore its parent transform
  nodeC_.GetObject()->get().GetFlags().SetFlag(
    SceneNodeFlags::kIgnoreParentTransform,
    SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Update transforms using convenience method
  const auto updated_count = GetTraversal().UpdateTransforms();

  // Assert: A, and B are dirty and should be updated
  EXPECT_EQ(updated_count, 2);
  EXPECT_FALSE(IsNodeTransformDirty(root_));
  EXPECT_FALSE(IsNodeTransformDirty(nodeA_));
  EXPECT_FALSE(IsNodeTransformDirty(nodeB_));
  EXPECT_FALSE(IsNodeTransformDirty(nodeC_)); // Still clean
}

//! Tests that UpdateTransformsFrom only updates nodes in the specified subtree.
NOLINT_TEST_F(SceneTraversalTransformTest, UpdateTransformsFromSpecificRoot)
{
  // The parent of A,B, and C is root, so it must have updated world
  // transforms, or the world update of A,B,C is meaningless and will make
  // assertions fail. This is done in Setup().
  EXPECT_FALSE(IsNodeTransformDirty(root_));

  // Arrange: Mark nodes as dirty in different subtrees
  MarkNodeTransformDirty(nodeA_);
  MarkNodeTransformDirty(nodeB_);
  MarkNodeTransformDirty(nodeC_);

  // Act: Update transforms only from nodeA's subtree
  std::vector<SceneNode> roots = { nodeA_ };
  const auto updated_count = GetTraversal().UpdateTransforms(roots);

  // Assert: Only A and C should be updated, B should remain dirty
  EXPECT_EQ(updated_count, 2);
  EXPECT_FALSE(IsNodeTransformDirty(nodeA_));
  EXPECT_FALSE(IsNodeTransformDirty(nodeC_));
  EXPECT_TRUE(IsNodeTransformDirty(nodeB_));
}

//=============================================================================
// High-Performance Filter Tests
//=============================================================================

class SceneTraversalBuiltinFilterTest : public SceneTraversalTransformTest {
protected:
  void SetUp() override
  {
    SceneTraversalTestBase::SetUp();
    // Create nodes with different visibility states
    visible_root_ = CreateNode("visible_root");
    invisible_node_ = CreateInvisibleNode("invisible");
    visible_child_ = CreateInvisibleChildNode(invisible_node_, "visible_child");

    // As a clean start, update the transforms of all nodes.
    UpdateSingleNodeTransforms(visible_root_);
    UpdateSingleNodeTransforms(invisible_node_);
    UpdateSingleNodeTransforms(visible_child_);
  }

  // Member variables using default constructor for SceneNode
  SceneNode visible_root_;
  SceneNode invisible_node_;
  SceneNode visible_child_;
};

//! Tests that only visible nodes are visited when using VisibleFilter.
NOLINT_TEST_F(SceneTraversalBuiltinFilterTest, VisibleFilter)
{
  // Act: Traverse with VisibleFilter
  const auto result = GetTraversal().Traverse(
    CreateTrackingVisitor(), TraversalOrder::kPreOrder, VisibleFilter {});

  // Assert: Only visible_root should be visited; invisible_node rejects
  // subtree
  ExpectTraversalResult(result, 1, 1, true);
  EXPECT_THAT(visit_order_, testing::ElementsAre("visible_root"));
}

//! Tests that only nodes with dirty transforms are visited when using
//! DirtyTransformFilter (builtin filter).
NOLINT_TEST_F(SceneTraversalBuiltinFilterTest, DirtyTransformFilter)
{
  // Setup: Mark some nodes as dirty
  MarkNodeTransformDirty(visible_root_);
  MarkNodeTransformDirty(visible_child_);

  // Act: Traverse with DirtyTransformFilter
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor(),
    TraversalOrder::kPreOrder, DirtyTransformFilter {});

  // Assert: Only nodes with dirty transforms should be visited
  ExpectTraversalResult(result, 2, 1, true);
  ExpectContainsExactlyNodes({ "visible_root", "visible_child" });
}

//=============================================================================
// Edge Cases and Error Handling Tests
//=============================================================================

class SceneTraversalEdgeCaseTest : public SceneTraversalTestBase { };

//! Tests that a deep hierarchy (chain) can be traversed without stack overflow
//! and all nodes are visited.
NOLINT_TEST_F(SceneTraversalEdgeCaseTest, DeepHierarchyTraversal)
{
  // Arrange: Create a deep linear hierarchy (chain)
  scene_->Clear();
  SceneNode current = CreateNode("node_0");
  for (int i = 1; i < 100; ++i) {
    current = CreateChildNode(current, "node_" + std::to_string(i));
  }

  // Act: Traverse the deep hierarchy
  const auto result = GetTraversal().Traverse(CreateTrackingVisitor());

  // Assert: All 100 nodes should be visited without stack overflow
  ExpectTraversalResult(result, 100, 0, true);
  EXPECT_EQ(visit_order_.size(), 100);
  EXPECT_EQ(visit_order_[0], "node_0");
  EXPECT_EQ(visit_order_[99], "node_99");
}

//! Tests that a wide hierarchy (many children at root) can be traversed and all
//! nodes are visited.
NOLINT_TEST_F(SceneTraversalEdgeCaseTest, WideHierarchyTraversal)
{
  // Arrange: Create a wide hierarchy (many children at root level)
  scene_->Clear();
  auto root = CreateNode("root");
  for (int i = 0; i < 100; ++i) {
    [[maybe_unused]] auto _
      = CreateChildNode(root, "child_" + std::to_string(i));
  }

  // Act: Traverse the wide hierarchy
  const auto result = GetTraversal().Traverse(
    CreateTrackingVisitor(), TraversalOrder::kBreadthFirst);

  // Assert: All 101 nodes should be visited (root + 100 children)
  ExpectTraversalResult(result, 101, 0, true);
  EXPECT_EQ(visit_order_.size(), 101);
  EXPECT_EQ(visit_order_[0], "root");
}

//! Tests that a filter rejecting all nodes results in no nodes visited and
//! correct filter count.
NOLINT_TEST_F(SceneTraversalEdgeCaseTest, FilterRejectingAllNodes)
{
  // Arrange: Create simple hierarchy
  auto root = CreateNode("root");
  [[maybe_unused]] auto _ = CreateChildNode(root, "child");
  // Act: Traverse with filter that rejects all nodes
  auto reject_all_filter
    = [](const MutableVisitedNode& /*visited_node*/,
        FilterResult /*parent_filter_result*/) -> FilterResult {
    return FilterResult::kRejectSubTree;
  };
  static_assert(
    oxygen::scene::MutatingSceneFilter<decltype(reject_all_filter)>);
  const auto result = GetTraversal().Traverse(
    CreateTrackingVisitor(), TraversalOrder::kPreOrder, reject_all_filter);

  // Assert: No nodes should be visited, but filter count depends on
  // implementation If the filter is applied to root and rejects the subtree,
  // only 1 node is "filtered" The child is never reached because the entire
  // subtree is rejected at root
  ExpectTraversalResult(result, 0, 1, true);
  EXPECT_TRUE(visit_order_.empty());
}

//! Tests that a visitor which stops immediately results in only the first node
//! being visited.
NOLINT_TEST_F(SceneTraversalEdgeCaseTest, VisitorStoppingImmediately)
{
  // Arrange: Create simple hierarchy
  auto root = CreateNode("root");
  [[maybe_unused]] auto _ = CreateChildNode(root, "child");
  // Act: Traverse with visitor that stops immediately
  auto immediate_stop_visitor
    = [this](const MutableVisitedNode& node, bool dry_run) -> VisitResult {
    if (!dry_run) {
      visit_order_.emplace_back(
        node.node_impl->GetName()); // Convert string_view to string
    }
    return VisitResult::kStop;
  };
  const auto result = GetTraversal().Traverse(immediate_stop_visitor);

  // Assert: Only first node should be visited
  ExpectTraversalResult(result, 1, 0, false);
  ExpectVisitedNodes({ "root" });
}

//=============================================================================
// Complex Scenario Tests
//=============================================================================

class SceneTraversalComplexTest : public SceneTraversalTransformTest {
protected:
  void SetUp() override
  {
    SceneTraversalTestBase::SetUp();
    CreateComplexHierarchy();
  }
  void CreateComplexHierarchy()
  {
    // Create a complex hierarchy:
    //         root
    //       /   |   \
    //      A    B    C (invisible)
    //     /|    |   /|\
    //    D E    F  G H I
    //      |       |
    //      J       K

    root_ = CreateNode("root");

    // Level 1
    nodeA_ = CreateChildNode(root_, "A");
    nodeB_ = CreateChildNode(root_, "B");
    nodeC_ = CreateInvisibleChildNode(root_, "C");

    // Level 2
    nodeD_ = CreateChildNode(nodeA_, "D");
    nodeE_ = CreateChildNode(nodeA_, "E");
    nodeF_ = CreateChildNode(nodeB_, "F");
    nodeG_ = CreateChildNode(nodeC_, "G");
    nodeH_ = CreateChildNode(nodeC_, "H");
    nodeI_ = CreateChildNode(nodeC_, "I");

    // Level 3
    nodeJ_ = CreateChildNode(nodeE_, "J");
    nodeK_ = CreateChildNode(nodeH_, "K");

    // As a clean start, update the transforms of all nodes.
    UpdateSingleNodeTransforms(root_);
    UpdateSingleNodeTransforms(nodeA_);
    UpdateSingleNodeTransforms(nodeB_);
    UpdateSingleNodeTransforms(nodeC_);
    UpdateSingleNodeTransforms(nodeD_);
    UpdateSingleNodeTransforms(nodeE_);
    UpdateSingleNodeTransforms(nodeF_);
    UpdateSingleNodeTransforms(nodeG_);
    UpdateSingleNodeTransforms(nodeH_);
    UpdateSingleNodeTransforms(nodeI_);
    UpdateSingleNodeTransforms(nodeJ_);
    UpdateSingleNodeTransforms(nodeK_);
  }

  // Member variables using default constructor for SceneNode
  SceneNode root_;
  SceneNode nodeA_;
  SceneNode nodeB_;
  SceneNode nodeC_;
  SceneNode nodeD_;
  SceneNode nodeE_;
  SceneNode nodeF_;
  SceneNode nodeG_;
  SceneNode nodeH_;
  SceneNode nodeI_;
  SceneNode nodeJ_;
  SceneNode nodeK_;
};

//! Tests that combining a visible filter and subtree skipping works as expected
//! in a complex hierarchy.
NOLINT_TEST_F(SceneTraversalComplexTest, CombinedFilterAndVisitorControl)
{
  // Act: Traverse with visible filter and subtree skipping at A
  const auto result = GetTraversal().Traverse(CreateSubtreeSkippingVisitor("A"),
    TraversalOrder::kPreOrder, VisibleFilter {});

  // Assert: Should visit root, A (but skip its subtree), B, F
  // C and its subtree should be filtered out by VisibleFilter
  ExpectTraversalResult(result, 4, 1, true);
  ExpectContainsExactlyNodes({ "root", "A", "B", "F" });
}

//! Tests that only dirty nodes are updated in a complex hierarchy using
//! UpdateTransforms().
NOLINT_TEST_F(SceneTraversalComplexTest, DirtyTransformUpdateInComplexHierarchy)
{
  // Arrange: Mark nodes as dirty for this test
  MarkNodeTransformDirty(nodeA_);
  MarkNodeTransformDirty(nodeF_);
  MarkNodeTransformDirty(nodeK_);

  // Act: Update only dirty transforms
  const auto updated_count = GetTraversal().UpdateTransforms();

  // Assert: Should update all dirty subtrees (A, D, E, J, F, K)
  EXPECT_EQ(updated_count, 6);
}

//! Tests that transform update with a custom filter (dirty and visible) only
//! updates nodes that are both dirty and visible.
NOLINT_TEST_F(SceneTraversalComplexTest, UpdateTransformsWithVisibleFilter)
{
  // Arrange: Mark B and K as dirty, but C is invisible (so K is invisible)
  MarkNodeTransformDirty(nodeB_);
  MarkNodeTransformDirty(
    nodeK_); // Custom filter: node must be dirty and visible
  auto dirty_and_visible
    = [](const MutableVisitedNode& visited_node,
        FilterResult /*parent_filter_result*/) -> FilterResult {
    const auto& node = *visited_node.node_impl;
    const auto& flags = node.GetFlags();
    const auto visible = flags.GetEffectiveValue(SceneNodeFlags::kVisible);
    std::cerr << "Node `" << node.GetName() << "` is "
              << (visible ? "visible" : "invisible") << " and "
              << (node.IsTransformDirty() ? "dirty" : "clean") << "\n";
    if (!visible) {
      return FilterResult::kRejectSubTree;
    }
    return node.IsTransformDirty() ? FilterResult::kAccept
                                   : FilterResult::kReject;
  };
  // Act: Update transforms with custom filter
  auto updated_names = std::vector<std::string> {};
  const auto updated_count = GetTraversal().Traverse(
    [this, &updated_names](
      const MutableVisitedNode& node, bool dry_run) -> VisitResult {
      if (!dry_run && node.node_impl->IsTransformDirty()) {
        node.node_impl->UpdateTransforms(*scene_);
        updated_names.emplace_back(node.node_impl->GetName());
      }
      return VisitResult::kContinue;
    },
    TraversalOrder::kPreOrder, dirty_and_visible);

  // Assert: Only B should be updated (K is invisible, F is not dirty, root is
  // not dirty, A and E are not dirty)
  EXPECT_THAT(updated_names, testing::UnorderedElementsAre("B"));
  EXPECT_EQ(updated_count.nodes_visited, 1); // 1 visible node visited: B
  EXPECT_FALSE(IsNodeTransformDirty(nodeB_));
  EXPECT_TRUE(IsNodeTransformDirty(nodeK_)); // K should remain dirty
}

} // namespace
