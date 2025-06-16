//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "Helpers/TestSceneFactory.h"
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
using oxygen::scene::testing::TestSceneFactory;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneGraphTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Arrange: Create test scene for all graph tests
    scene_ = std::make_shared<Scene>("TestScene", 1024);
  }

  void TearDown() override
  {
    // Clean up: Reset scene pointer to ensure proper cleanup
    scene_.reset();
  }

  // Helper: Collect all children of a node into a set for order-agnostic
  // comparison
  static auto CollectChildrenHandles(SceneNode& parent) -> std::set<NodeHandle>
  {
    auto children = std::set<NodeHandle> {};
    auto current = parent.GetFirstChild();
    while (current.has_value()) {
      children.insert(current->GetHandle());
      current = current->GetNextSibling();
    }
    return children;
  }
  // Helper: Verify parent-child relationship exists
  static void ExpectParentChildRelationship(SceneNode& parent, SceneNode& child)
  {
    // Child should know its parent
    const auto child_parent = child.GetParent();
    ASSERT_TRUE(child_parent.has_value()) << "Child should have a parent";
    EXPECT_EQ(child_parent->GetHandle(), parent.GetHandle())
      << "Child's parent should match expected parent";

    // Parent should have this child in its children list
    const auto children = CollectChildrenHandles(parent);
    EXPECT_TRUE(children.contains(child.GetHandle()))
      << "Parent should contain this child";
  }
  // Helper: Verify hierarchy state flags for a node
  static void ExpectHierarchyState(SceneNode& node, const bool is_root,
    const bool has_parent, const bool has_children)
  {
    EXPECT_EQ(node.IsRoot(), is_root)
      << "Node root status should match expected";
    EXPECT_EQ(node.HasParent(), has_parent)
      << "Node parent status should match expected";
    EXPECT_EQ(node.HasChildren(), has_children)
      << "Node children status should match expected";
  }

  // Helper: Create a simple parent with N children and return all nodes
  [[nodiscard]] auto CreateSimpleFamily(const std::string& parent_name,
    const std::vector<std::string>& child_names) const
    -> std::pair<SceneNode, std::vector<SceneNode>>
  {
    auto parent = scene_->CreateNode(parent_name);
    EXPECT_TRUE(parent.IsValid()) << "Parent creation should succeed";
    auto children = std::vector<SceneNode> {};

    for (const auto& child_name : child_names) {
      auto child_opt = scene_->CreateChildNode(parent, child_name);
      EXPECT_TRUE(child_opt.has_value()) << "Child creation should succeed";
      if (child_opt.has_value()) {
        children.push_back(child_opt.value());
      }
    }

    return { parent, children };
  }

  // Helper: Verify all nodes in a list have the same parent
  static void ExpectAllHaveSameParent(
    std::vector<SceneNode> nodes, SceneNode& expected_parent)
  {
    for (auto& node : nodes) {
      ExpectParentChildRelationship(expected_parent, node);
    }
  }

  // Helper: Verify expected children count matches actual
  void ExpectChildrenCount(
    SceneNode& parent, const std::size_t expected_count) const
  {
    const auto children = CollectChildrenHandles(parent);
    EXPECT_EQ(children.size(), expected_count)
      << "Parent should have expected number of children";
    EXPECT_EQ(scene_->GetChildrenCount(parent), expected_count)
      << "Scene API should report same children count";
  }

  std::shared_ptr<Scene> scene_;
};

//------------------------------------------------------------------------------
// Basic Hierarchy Relationship Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, ParentChildRelationship_BasicNavigationWorks)
{
  // Arrange: Create parent and child nodes
  auto parent = scene_->CreateNode("Parent");
  auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto& child = child_opt.value();

  // Act: Verify parent-child relationship

  // Assert: Navigation should work in both directions
  TRACE_GCHECK_F(
    ExpectParentChildRelationship(parent, child), "parent-child-link");

  // Assert: Hierarchy state should be correct
  TRACE_GCHECK_F(ExpectHierarchyState(parent, true, false, true),
    "parent-state"); // root with children
  TRACE_GCHECK_F(ExpectHierarchyState(child, false, true, false),
    "child-state"); // has parent, no children
}

NOLINT_TEST_F(SceneGraphTest, SingleChild_ParentNavigationWorks)
{
  // Arrange: Create simple parent-child relationship
  auto [parent, children] = CreateSimpleFamily("Parent", { "OnlyChild" });
  ASSERT_EQ(children.size(), 1);
  auto only_child = children[0];

  // Act: Check parent's first child navigation
  const auto first_child = parent.GetFirstChild();

  // Assert: Parent should find the only child
  ASSERT_TRUE(first_child.has_value());
  EXPECT_EQ(first_child->GetHandle(), only_child.GetHandle());

  // Assert: Only child should have no siblings
  EXPECT_FALSE(only_child.GetNextSibling().has_value());
  EXPECT_FALSE(only_child.GetPrevSibling().has_value());
}

NOLINT_TEST_F(SceneGraphTest, MultipleChildren_SiblingNavigationWorks)
{
  // Arrange: Create parent with multiple children
  auto [parent, children]
    = CreateSimpleFamily("Parent", { "Child1", "Child2", "Child3" });
  ASSERT_EQ(children.size(), 3);

  // Act: Collect children through sibling navigation
  const auto found_children = CollectChildrenHandles(parent);
  auto expected_children = std::set<NodeHandle> {};
  for (const auto& child : children) {
    expected_children.insert(child.GetHandle());
  }

  // Assert: All children should be found through navigation
  EXPECT_EQ(found_children, expected_children);

  // Assert: All children should have the same parent
  TRACE_GCHECK_F(ExpectAllHaveSameParent(children, parent), "same-parent");

  // Assert: Parent should report correct children count
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 3), "children-count");
}

NOLINT_TEST_F(
  SceneGraphTest, SiblingConsistency_ForwardAndBackwardNavigationMatch)
{
  // Arrange: Create parent with exactly 2 children for predictable testing
  auto [parent, children]
    = CreateSimpleFamily("Parent", { "FirstChild", "SecondChild" });
  ASSERT_EQ(children.size(), 2);

  // Act: Get first child and navigate to second
  auto first_child = parent.GetFirstChild();
  ASSERT_TRUE(first_child.has_value());
  auto second_child_opt = first_child->GetNextSibling();
  ASSERT_TRUE(second_child_opt.has_value());
  auto& second_child = second_child_opt.value();

  // Act: Navigate back from second to first
  const auto back_to_first_opt = second_child.GetPrevSibling();
  ASSERT_TRUE(back_to_first_opt.has_value());
  const auto& back_to_first = back_to_first_opt.value();

  // Assert: Forward and backward navigation should be consistent
  EXPECT_EQ(first_child->GetHandle(), back_to_first.GetHandle());

  // Assert: Boundary conditions
  EXPECT_FALSE(
    first_child->GetPrevSibling().has_value()); // First has no previous
  EXPECT_FALSE(second_child.GetNextSibling().has_value()); // Second has no next
}

//------------------------------------------------------------------------------
// Root Node Behavior Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, RootNodes_BehaviorIsCorrect)
{
  // Arrange: Create multiple independent root nodes
  auto root1 = scene_->CreateNode("Root1");
  auto root2 = scene_->CreateNode("Root2");

  // Act: Check root node properties
  // Assert: Root nodes should have correct hierarchy state
  TRACE_GCHECK_F(
    ExpectHierarchyState(root1, true, false, false), "root1-state");
  TRACE_GCHECK_F(
    ExpectHierarchyState(root2, true, false, false), "root2-state");

  // Assert: Root nodes should have no navigation options
  EXPECT_FALSE(root1.GetParent().has_value());
  EXPECT_FALSE(root1.GetFirstChild().has_value());
  EXPECT_FALSE(root1.GetNextSibling().has_value());
  EXPECT_FALSE(root1.GetPrevSibling().has_value());
}

NOLINT_TEST_F(SceneGraphTest, RootNodeCollection_AllRootsFound)
{
  // Arrange: Create multiple root nodes
  auto root1 = scene_->CreateNode("Root1");
  const auto root2 = scene_->CreateNode("Root2");
  const auto root3 = scene_->CreateNode("Root3");

  // Arrange: Create one child to verify it doesn't appear in root collection
  const auto child_opt = scene_->CreateChildNode(root1, "Child");
  ASSERT_TRUE(child_opt.has_value());

  // Act: Get root nodes from scene
  auto root_handles = scene_->GetRootHandles();

  // Assert: Should find exactly the root nodes (not the child)
  EXPECT_EQ(root_handles.size(), 3);
  const auto expected_roots
    = std::set { root1.GetHandle(), root2.GetHandle(), root3.GetHandle() };
  const auto found_roots = std::set(root_handles.begin(), root_handles.end());
  EXPECT_EQ(found_roots, expected_roots);
}

NOLINT_TEST_F(SceneGraphTest, RootNodeDestruction_RemovedFromRootCollection)
{
  // Arrange: Create multiple root nodes
  const auto root1 = scene_->CreateNode("Root1");
  auto root2 = scene_->CreateNode("Root2");
  const auto root3 = scene_->CreateNode("Root3");

  // Arrange: Verify initial root collection
  const auto initial_roots = scene_->GetRootHandles();
  EXPECT_EQ(initial_roots.size(), 3);

  // Act: Destroy one root node
  const auto destroyed = scene_->DestroyNode(root2);

  // Assert: Destruction should succeed
  EXPECT_TRUE(destroyed);
  EXPECT_FALSE(root2.IsValid());

  // Assert: Root collection should be updated (this would fail with the
  // original bug)
  auto remaining_roots = scene_->GetRootHandles();
  EXPECT_EQ(remaining_roots.size(), 2);

  const auto expected_remaining
    = std::set { root1.GetHandle(), root3.GetHandle() };
  const auto found_remaining
    = std::set(remaining_roots.begin(), remaining_roots.end());
  EXPECT_EQ(found_remaining, expected_remaining);

  // Assert: Destroyed root should not be in the collection
  auto destroyed_handle = root2.GetHandle();
  EXPECT_TRUE(std::ranges::none_of(
    remaining_roots, [destroyed_handle](const auto& handle) {
      return handle == destroyed_handle;
    }));
}

//------------------------------------------------------------------------------
// Complex Hierarchy Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, ThreeGenerationHierarchy_NavigationWorks)
{
  // Arrange: Create root -> child -> grandchild hierarchy
  auto root = scene_->CreateNode("Root");
  auto child_opt = scene_->CreateChildNode(root, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto& child = child_opt.value();
  auto grandchild_opt = scene_->CreateChildNode(child, "Grandchild");
  ASSERT_TRUE(grandchild_opt.has_value());
  auto& grandchild = grandchild_opt.value();

  // Act: Verify hierarchy relationships
  // Assert: Root level (has children, no parent)
  TRACE_GCHECK_F(ExpectHierarchyState(root, true, false, true), "root-state");
  TRACE_GCHECK_F(ExpectParentChildRelationship(root, child), "root-child");

  // Assert: Middle level (has parent and children)
  TRACE_GCHECK_F(ExpectHierarchyState(child, false, true, true), "child-state");
  TRACE_GCHECK_F(ExpectParentChildRelationship(root, child), "root-child-rel");
  TRACE_GCHECK_F(ExpectParentChildRelationship(child, grandchild),
    "child-grandchild"); // NOLINT(readability-suspicious-call-argument)

  // Assert: Leaf level (has parent, no children)
  TRACE_GCHECK_F(
    ExpectHierarchyState(grandchild, false, true, false), "grandchild-state");
  TRACE_GCHECK_F(ExpectParentChildRelationship(child, grandchild),
    "child-grandchild-rel"); // NOLINT(readability-suspicious-call-argument)
}

NOLINT_TEST_F(SceneGraphTest, ComplexTreeStructure_TopologyIsCorrect)
{
  // Arrange: Build tree: Root -> (Child1, Child2) where Child1 has 2
  // grandchildren, Child2 has 1
  auto root = scene_->CreateNode("Root");

  auto child1_opt = scene_->CreateChildNode(root, "Child1");
  auto child2_opt = scene_->CreateChildNode(root, "Child2");
  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  auto child1 = child1_opt.value();
  auto child2 = child2_opt.value();

  auto grandchild1_opt = scene_->CreateChildNode(child1, "GrandChild1");
  auto grandchild2_opt = scene_->CreateChildNode(child1, "GrandChild2");
  auto grandchild3_opt = scene_->CreateChildNode(child2, "GrandChild3");
  ASSERT_TRUE(grandchild1_opt.has_value());
  ASSERT_TRUE(grandchild2_opt.has_value());
  ASSERT_TRUE(grandchild3_opt.has_value());
  auto grandchild1 = grandchild1_opt.value();
  auto grandchild2 = grandchild2_opt.value();
  auto& grandchild3 = grandchild3_opt.value();

  // Act: Verify complete tree structure

  // Assert: Total node count
  EXPECT_EQ(scene_->GetNodeCount(), 6);
  // Assert: Root level verification
  TRACE_GCHECK_F(ExpectHierarchyState(root, true, false, true), "root-state");
  TRACE_GCHECK_F(ExpectChildrenCount(root, 2), "root-children");
  TRACE_GCHECK_F(
    ExpectAllHaveSameParent({ child1, child2 }, root), "root-parent");

  // Assert: Child1 branch verification
  TRACE_GCHECK_F(
    ExpectHierarchyState(child1, false, true, true), "child1-state");
  TRACE_GCHECK_F(ExpectChildrenCount(child1, 2), "child1-children");
  TRACE_GCHECK_F(ExpectAllHaveSameParent({ grandchild1, grandchild2 }, child1),
    "child1-parent");

  // Assert: Child2 branch verification
  TRACE_GCHECK_F(
    ExpectHierarchyState(child2, false, true, true), "child2-state");
  TRACE_GCHECK_F(ExpectChildrenCount(child2, 1), "child2-children");
  TRACE_GCHECK_F(ExpectParentChildRelationship(child2, grandchild3),
    "child2-grandchild3"); // NOLINT(readability-suspicious-call-argument)

  // Assert: Leaf nodes verification
  TRACE_GCHECK_F(
    ExpectHierarchyState(grandchild1, false, true, false), "gc1-state");
  TRACE_GCHECK_F(
    ExpectHierarchyState(grandchild2, false, true, false), "gc2-state");
  TRACE_GCHECK_F(
    ExpectHierarchyState(grandchild3, false, true, false), "gc3-state");
}

//------------------------------------------------------------------------------
// Scene API Integration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, SceneHierarchyAPI_MatchesDirectNavigation)
{
  // Arrange: Create a simple family for API testing
  auto [parent, children]
    = CreateSimpleFamily("Parent", { "Child1", "Child2" });
  ASSERT_EQ(children.size(), 2);

  // Act: Test Scene API methods
  auto parent_from_scene = scene_->GetParent(children[0]);
  auto first_child_from_scene = scene_->GetFirstChild(parent);
  auto scene_children = scene_->GetChildren(parent);

  // Assert: Scene API should match direct navigation
  ASSERT_TRUE(parent_from_scene.has_value());
  EXPECT_EQ(parent_from_scene->GetHandle(), parent.GetHandle());

  ASSERT_TRUE(first_child_from_scene.has_value());
  auto direct_first_child = parent.GetFirstChild();
  ASSERT_TRUE(direct_first_child.has_value());
  EXPECT_EQ(
    first_child_from_scene->GetHandle(), direct_first_child->GetHandle());

  // Assert: Scene children collection should match navigation
  auto expected_children = CollectChildrenHandles(parent);
  auto scene_children_set
    = std::set(scene_children.begin(), scene_children.end());
  EXPECT_EQ(scene_children_set, expected_children);
}

NOLINT_TEST_F(
  SceneGraphTest, ChildrenCountAndEnumeration_IncrementalVerification)
{ // Arrange: Start with parent and no children
  auto parent = scene_->CreateNode("Parent");
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 0), "initial-count");
  auto children = scene_->GetChildren(parent);
  EXPECT_TRUE(children.empty());

  // Act & Assert: Add children one by one and verify count increases
  const auto child1_opt = scene_->CreateChildNode(parent, "Child1");
  ASSERT_TRUE(child1_opt.has_value());
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 1), "after-child1");

  const auto child2_opt = scene_->CreateChildNode(parent, "Child2");
  ASSERT_TRUE(child2_opt.has_value());
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 2), "after-child2");

  const auto child3_opt = scene_->CreateChildNode(parent, "Child3");
  ASSERT_TRUE(child3_opt.has_value());
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 3), "after-child3");

  // Act: Get final children collection
  children = scene_->GetChildren(parent);

  // Assert: Final verification of all children
  const auto expected_handles = std::set { child1_opt->GetHandle(),
    child2_opt->GetHandle(), child3_opt->GetHandle() };
  const auto found_handles = std::set(children.begin(), children.end());
  EXPECT_EQ(found_handles, expected_handles);
}

//------------------------------------------------------------------------------
// Hierarchy Manipulation and Edge Cases Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, NodeDestruction_RemovesFromHierarchy)
{
  // Arrange: Create parent with child
  auto parent = scene_->CreateNode("Parent");
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = child_opt.value();
  // Arrange: Verify initial relationship
  TRACE_GCHECK_F(
    ExpectParentChildRelationship(parent, child), "initial-relation");
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 1), "initial-count");

  // Act: Destroy child node
  scene_->DestroyNode(child);

  // Assert: Child should be invalid and removed from parent's children
  EXPECT_FALSE(child.IsValid());
  TRACE_GCHECK_F(ExpectChildrenCount(parent, 0), "after-destroy");
  EXPECT_FALSE(parent.GetFirstChild().has_value());
}

NOLINT_TEST_F(SceneGraphTest, InvalidNodeNavigation_ReturnsEmptyOptionals)
{
  // Arrange: Create node then destroy its validity
  auto parent = scene_->CreateNode("Parent");
  const auto child_opt = scene_->CreateChildNode(parent, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = child_opt.value();

  // Act: Destroy child to make it invalid
  scene_->DestroyNode(child);

  // Assert: Invalid node should return empty optional for all navigation
  EXPECT_FALSE(child.GetParent().has_value());
  EXPECT_FALSE(child.GetFirstChild().has_value());
  EXPECT_FALSE(child.GetNextSibling().has_value());
  EXPECT_FALSE(child.GetPrevSibling().has_value());

  // Assert: Invalid node hierarchy queries should return false
  EXPECT_FALSE(child.HasParent());
  EXPECT_FALSE(child.HasChildren());
  EXPECT_TRUE(child.IsRoot()); // Invalid parent, means no parent
}

NOLINT_TEST_F(SceneGraphTest, HierarchicalDestruction_AllDescendantsInvalidated)
{
  // Arrange: Create three-generation hierarchy
  auto root = scene_->CreateNode("Root");
  const auto child_opt = scene_->CreateChildNode(root, "Child");
  ASSERT_TRUE(child_opt.has_value());
  auto child = child_opt.value();
  const auto grandchild_opt = scene_->CreateChildNode(child, "Grandchild");
  ASSERT_TRUE(grandchild_opt.has_value());
  auto grandchild = grandchild_opt.value();
  // Arrange: Verify initial hierarchy
  EXPECT_EQ(scene_->GetNodeCount(), 3);
  TRACE_GCHECK_F(ExpectParentChildRelationship(root, child), "root-child");
  TRACE_GCHECK_F(ExpectParentChildRelationship(child, grandchild),
    "child-grandchild"); // NOLINT(readability-suspicious-call-argument)

  // Act: Destroy entire hierarchy starting from root
  const auto destroy_result = scene_->DestroyNodeHierarchy(root);

  // Assert: Destruction should succeed
  EXPECT_TRUE(destroy_result);

  // Assert: All nodes should become invalid
  EXPECT_FALSE(root.GetObject().has_value());
  EXPECT_FALSE(child.GetObject().has_value());
  EXPECT_FALSE(grandchild.GetObject().has_value());
}

NOLINT_TEST_F(SceneGraphTest, HierarchicalDestruction_RootCollectionUpdated)
{
  // Arrange: Create multiple root hierarchies
  auto root1 = scene_->CreateNode("Root1");
  const auto root2 = scene_->CreateNode("Root2");

  // Arrange: Add children to root1
  const auto child1_opt = scene_->CreateChildNode(root1, "Child1");
  const auto child2_opt = scene_->CreateChildNode(root1, "Child2");
  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());

  // Arrange: Verify initial state
  const auto initial_roots = scene_->GetRootHandles();
  EXPECT_EQ(initial_roots.size(), 2);
  EXPECT_EQ(scene_->GetNodeCount(), 4); // 2 roots + 2 children

  // Act: Destroy root1 hierarchy
  const auto destroy_result = scene_->DestroyNodeHierarchy(root1);

  // Assert: Destruction should succeed
  EXPECT_TRUE(destroy_result);

  // Assert: Root collection should be updated (this would fail with the
  // original bug)
  const auto remaining_roots = scene_->GetRootHandles();
  EXPECT_EQ(remaining_roots.size(), 1);
  EXPECT_EQ(remaining_roots[0], root2.GetHandle());

  // Assert: Only root2 should remain
  EXPECT_EQ(scene_->GetNodeCount(), 1);
  EXPECT_TRUE(root2.IsValid());
  EXPECT_FALSE(root1.IsValid());
}

//------------------------------------------------------------------------------
// Deep Hierarchy Navigation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, DeepHierarchy_NavigationPerformance)
{
  // Arrange: Create a chain of 10 nodes for depth testing
  auto current = scene_->CreateNode("Root");
  auto nodes = std::vector { current };
  for (int i = 1; i < 10; ++i) {
    auto child_opt
      = scene_->CreateChildNode(current, "Node" + std::to_string(i));
    ASSERT_TRUE(child_opt.has_value());
    current = child_opt.value();
    nodes.push_back(current);
  }

  // Act: Verify chain integrity by navigating from root to leaf
  auto nav_current = nodes[0]; // Start at root
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    auto child = nav_current.GetFirstChild();
    ASSERT_TRUE(child.has_value()) << "Navigation should work at depth " << i;
    EXPECT_EQ(child->GetHandle(), nodes[i].GetHandle())
      << "Navigation should find correct child at depth " << i;
    nav_current = child.value();
  }

  // Assert: Navigate back up from leaf to root
  nav_current = nodes.back(); // Start at leaf
  for (int i = static_cast<int>(nodes.size()) - 2; i >= 0; --i) {
    auto parent = nav_current.GetParent();
    ASSERT_TRUE(parent.has_value()) << "Navigation should work at depth " << i;
    EXPECT_EQ(parent->GetHandle(), nodes[i].GetHandle())
      << "Navigation should find correct parent at depth " << i;
    nav_current = parent.value();
  }
}

//------------------------------------------------------------------------------
// Large Family Navigation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneGraphTest, LargeFamily_SiblingNavigationCompletes)
{
  // Arrange: Create parent with many children (testing sibling list
  // integrity)
  constexpr std::size_t child_count = 50;
  auto parent = scene_->CreateNode("Parent");
  auto children = std::vector<SceneNode> {};

  for (std::size_t i = 0; i < child_count; ++i) {
    auto child_opt
      = scene_->CreateChildNode(parent, "Child" + std::to_string(i));
    ASSERT_TRUE(child_opt.has_value());
    children.push_back(child_opt.value());
  }

  // Act: Navigate through all siblings and count them
  auto found_count = std::size_t { 0 };
  auto current = parent.GetFirstChild();
  while (current.has_value()) {
    ++found_count;
    current = current->GetNextSibling();
  }

  // Assert: Should find all children through sibling navigation
  EXPECT_EQ(found_count, child_count);
  TRACE_GCHECK_F(ExpectChildrenCount(parent, child_count), "children-count");

  // Assert: All children should still have correct parent
  for (auto& child : children) {
    TRACE_GCHECK_F(
      ExpectParentChildRelationship(parent, child), child.GetName());
  }
}

} // namespace
