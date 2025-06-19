//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <deque>
#include <list>
#include <vector>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  //=== Immediate Mode Test Fixture ===---------------------------------------//

  class SceneQueryImmediateTest : public SceneQueryTestBase { };

  //=== FindFirst Tests ===---------------------------------------------------//

  NOLINT_TEST_F(
    SceneQueryImmediateTest, FindFirst_WithMatchingPredicate_ReturnsFirstMatch)
  {
    // Arrange: Use complex hierarchy from JSON
    // Hierarchy: World -> Environment -> Buildings -> [House1, House2, Office]
    CreateComplexHierarchyFromJson();

    // Act: Find first building
    auto result = query_->FindFirst(NodeNameStartsWith("House"));

    // Assert: Should find House1 (first in traversal order)
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "House1");
  }

  NOLINT_TEST_F(SceneQueryImmediateTest, FindFirst_WithNoMatches_ReturnsNullopt)
  {
    // Arrange: Complex hierarchy loaded
    CreateComplexHierarchyFromJson();

    // Act: Search for non-existent node
    auto result = query_->FindFirst(NodeNameEquals("NonExistentNode"));

    // Assert: Should return nullopt
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, FindFirst_WithMultipleMatches_ReturnsFirst)
  {
    // Arrange: Create scene with multiple "Enemy" nodes using TestSceneFactory
    CreateGameSceneHierarchy();

    // Act: Find first enemy
    auto result = query_->FindFirst(NodeNameStartsWith("Enemy"));

    // Assert: Should return Enemy1 (first in traversal order)
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "Enemy1");
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, FindFirst_WithComplexHierarchy_TraversesCorrectly)
  {
    // Arrange: Complex hierarchy from JSON template
    CreateComplexHierarchyFromJson();

    // Act: Find deeply nested node
    auto result = query_->FindFirst(NodeNameEquals("Office"));

    // Assert: Should find the Office node deep in the hierarchy
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "Office");
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, FindFirst_WithRootNode_FindsImmediately)
  {
    // Arrange: Complex hierarchy with "World" root
    CreateComplexHierarchyFromJson();

    // Act: Find root node
    auto result = query_->FindFirst(NodeNameEquals("World"));

    // Assert: Should find World immediately
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "World");
  }

  //=== Collect Tests ===-----------------------------------------------------//

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Collect_WithMatchingPredicate_CollectsAllMatches)
  {
    // Arrange: Use game scene hierarchy with multiple enemies
    CreateGameSceneHierarchy();
    std::vector<SceneNode> enemies;

    // Act: Collect all enemy nodes
    auto result = query_->Collect(enemies, NodeNameStartsWith("Enemy"));

    // Assert: Should collect all 3 enemies
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(enemies.size(), 3);
    ExpectNodesWithNames(enemies, { "Enemy1", "Enemy2", "Enemy3" });
    EXPECT_EQ(result.nodes_matched, 3);
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Collect_WithNoMatches_ReturnsEmptyContainer)
  {
    // Arrange: Complex hierarchy
    CreateComplexHierarchyFromJson();
    std::vector<SceneNode> nodes;

    // Act: Collect non-existent nodes
    auto result = query_->Collect(nodes, NodeNameEquals("NonExistent"));

    // Assert: Should return empty container
    EXPECT_TRUE(result.completed);
    EXPECT_TRUE(nodes.empty());
    EXPECT_EQ(result.nodes_matched, 0);
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Collect_WithDifferentContainerTypes_WorksCorrectly)
  {
    // Arrange: Game scene with multiple items
    CreateGameSceneHierarchy();

    // Act: Test different container types
    std::vector<SceneNode> vector_nodes;
    std::deque<SceneNode> deque_nodes;
    std::list<SceneNode> list_nodes;

    auto result1 = query_->Collect(vector_nodes, NodeNameStartsWith("Potion"));
    auto result2 = query_->Collect(deque_nodes, NodeNameStartsWith("Potion"));
    auto result3 = query_->Collect(list_nodes, NodeNameStartsWith("Potion"));

    // Assert: All container types should work
    EXPECT_TRUE(result1.completed);
    EXPECT_TRUE(result2.completed);
    EXPECT_TRUE(result3.completed);

    EXPECT_EQ(vector_nodes.size(), 2);
    EXPECT_EQ(deque_nodes.size(), 2);
    EXPECT_EQ(list_nodes.size(), 2);
  }

  NOLINT_TEST_F(SceneQueryImmediateTest,
    Collect_WithPreallocatedContainer_PreservesExistingElements)
  {
    // Arrange: Game scene with multiple potions
    CreateGameSceneHierarchy();

    // Arrange: Create extra node manually and add to container
    auto extra_node = CreateVisibleNode("ExtraNode");
    std::vector<SceneNode> nodes;
    nodes.push_back(extra_node);

    // Act: Collect potions into pre-filled container
    auto result = query_->Collect(nodes, NodeNameStartsWith("Potion"));

    // Assert: Should preserve existing element and add new ones
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(nodes.size(), 3); // 1 existing + 2 potions
    EXPECT_EQ(nodes[0].GetName(), "ExtraNode"); // Original element preserved
  }

  //=== Count Tests ===-------------------------------------------------------//

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Count_WithMatchingPredicate_ReturnsCorrectCount)
  {
    // Arrange: Game scene hierarchy
    CreateGameSceneHierarchy();

    // Act: Count all visible nodes
    auto result = query_->Count(NodeIsVisible());

    // Assert: Should count all visible nodes correctly
    EXPECT_TRUE(result.completed);
    EXPECT_GT(result.nodes_matched, 0);
    EXPECT_GT(result.nodes_examined, result.nodes_matched);
  }

  NOLINT_TEST_F(SceneQueryImmediateTest, Count_WithNoMatches_ReturnsZero)
  {
    // Arrange: Complex hierarchy
    CreateComplexHierarchyFromJson();

    // Act: Count non-existent nodes
    auto result = query_->Count(NodeNameEquals("NonExistent"));

    // Assert: Should return zero count
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.nodes_matched, 0);
    EXPECT_GT(result.nodes_examined, 0); // Should still examine nodes
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Count_WithLargeHierarchy_CountsEfficiently)
  {
    // Arrange: Create large forest using TestSceneFactory
    CreateForestScene(5, 10); // 5 roots with 10 children each = 55+ nodes

    // Act: Count all nodes
    auto result = query_->Count([](const ConstVisitedNode&) { return true; });

    // Assert: Should count all nodes efficiently
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.nodes_matched, result.nodes_examined);
    EXPECT_GT(result.nodes_matched, 50); // At least 5 roots + 50 children
  }

  //=== Any Tests ===--------------------------------------------------------//

  NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithMatchingPredicate_ReturnsTrue)
  {
    // Arrange: Complex hierarchy with known node
    CreateComplexHierarchyFromJson();

    // Act: Check if any node is named "World"
    auto result = query_->Any(NodeNameEquals("World"));

    // Assert: Should return true
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
  }

  NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithNoMatches_ReturnsFalse)
  {
    // Arrange: Complex hierarchy
    CreateComplexHierarchyFromJson();

    // Act: Check for non-existent node
    auto result = query_->Any(NodeNameEquals("NonExistent"));

    // Assert: Should return false
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Any_WithFirstNodeMatching_ReturnsImmediately)
  {
    // Arrange: Create linear chain where root matches
    CreateLinearChainScene(10); // Deep chain for early termination test

    // Act: Search for root node (should terminate immediately)
    auto result = query_->Any(NodeNameEquals("Root"));

    // Assert: Should return true immediately
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
  }

  NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithInvisibleNodes_FindsCorrectly)
  {
    // Arrange: Game scene with mixed visibility
    CreateGameSceneHierarchy();

    // Act: Check if any invisible nodes exist
    auto result = query_->Any(NodeIsInvisible());

    // Assert: Should find invisible Enemy2
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
  }

  //=== Edge Cases and Error Conditions ===----------------------------------//

  NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithEmptyScene_HandlesGracefully)
  {
    // Arrange: Create empty scene
    scene_->Clear();
    ASSERT_TRUE(scene_->IsEmpty());
    CreateQuery();

    // Act: Perform various queries on empty scene
    auto find_result = query_->FindFirst(NodeNameEquals("Any"));
    auto any_result = query_->Any(NodeNameEquals("Any"));
    auto count_result = query_->Count(NodeNameEquals("Any"));

    std::vector<SceneNode> nodes;
    auto collect_result = query_->Collect(nodes, NodeNameEquals("Any"));

    // Assert: All operations should complete gracefully
    EXPECT_FALSE(find_result.has_value());
    ASSERT_TRUE(any_result.has_value());
    EXPECT_FALSE(any_result.value());
    EXPECT_TRUE(count_result.completed);
    EXPECT_EQ(count_result.nodes_matched, 0);
    EXPECT_TRUE(collect_result.completed);
    EXPECT_TRUE(nodes.empty());
  }

  NOLINT_TEST_F(
    SceneQueryImmediateTest, Query_WithSingleNodeScene_WorksCorrectly)
  {
    // Arrange: Create simple single node scene
    CreateSimpleScene();

    // Act: Query the single node
    auto find_result = query_->FindFirst(NodeNameEquals("Root"));
    auto count_result
      = query_->Count([](const ConstVisitedNode&) { return true; });

    // Assert: Should find the single node
    ASSERT_TRUE(find_result.has_value());
    EXPECT_EQ(find_result->GetName(), "Root");
    EXPECT_TRUE(count_result.completed);
    EXPECT_EQ(count_result.nodes_matched, 1);
  }

} // namespace

} // namespace oxygen::scene::testing
