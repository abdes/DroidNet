//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <deque>
#include <list>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "SceneQueryTestBase.h"

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::SceneNode;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Immediate Mode Test Fixture ===---------------------------------------//

class SceneQueryImmediateTest : public SceneQueryTestBase {
  void SetUp() override
  {
    // Create game scene hierarchy suitable for batch testing
    CreateGameSceneHierarchy();
  }

  auto CreateGameSceneHierarchy() -> void
  {
    const auto json = GetGameSceneJson();
    scene_ = GetFactory().CreateFromJson(json, "GameScene");
    ASSERT_NE(scene_, nullptr);
    CreateQuery();
  }

  auto GetGameSceneJson() -> std::string
  {
    return R"({
      "metadata": {
        "name": "MultiPlayerHierarchy"
      },
      "nodes": [
        {
          "name": "GameWorld",
          "children": [
            {
              "name": "Player1",
              "flags": {"visible": true},
              "children": [
                {"name": "Weapon", "flags": {"visible": true}},
                {"name": "Shield", "flags": {"visible": true}},
                {"name": "Armor", "flags": {"visible": false}}
              ]
            },
            {
              "name": "Player2",
              "flags": {"visible": true},
              "children": [
                {"name": "Weapon", "flags": {"visible": true}},
                {"name": "Bow", "flags": {"visible": true}},
                {"name": "Quiver", "flags": {"visible": false}}
              ]
            },
            {
              "name": "NPCs",
              "children": [
                {"name": "Merchant", "flags": {"visible": true}},
                {"name": "Guard", "flags": {"visible": true}}
              ]
            },
            {
              "name": "Environment",
              "children": [
                {"name": "Tree1", "flags": {"visible": true}},
                {"name": "Tree2", "flags": {"visible": true}},
                {"name": "Rock", "flags": {"visible": true}}
              ]
            }
          ]
        }
      ]
    })";
  }
};

//=== FindFirst Tests ===---------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryImmediateTest, FindFirst_WithMatchingPredicate_ReturnsFirstMatch)
{
  // Arrange: Use default game scene hierarchy

  // Act: Find first tree
  auto result = query_->FindFirst(NodeNameStartsWith("Tree"));

  // Assert: Should find House1 (first in traversal order)
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Tree1");
}

NOLINT_TEST_F(SceneQueryImmediateTest, FindFirst_WithNoMatches_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy

  // Act: Search for non-existent node
  auto result = query_->FindFirst(NodeNameEquals("NonExistentNode"));

  // Assert: Should return nullopt
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(SceneQueryImmediateTest, FindFirst_WithRootNode_FindsImmediately)
{
  // Arrange: Use default game scene hierarchy

  // Act: Find root node
  auto result = query_->FindFirst(NodeNameEquals("GameWorld"));

  // Assert: Should find World immediately
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "GameWorld");
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, FindFirst_WithScopedTraversal_FindsDifferentNodes)
{
  // Arrange: Use default game scene hierarchy

  // Find Player1 and Player2 nodes
  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  auto player2_node = query_->FindFirst(NodeNameEquals("Player2"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(player2_node.has_value());

  // Act: Find weapon scoped to Player1
  query_->AddToTraversalScope(*player1_node);
  auto player1_weapon = query_->FindFirst(NodeNameEquals("Weapon"));
  // Reset and scope to Player2
  query_->ResetTraversalScope();
  query_->AddToTraversalScope(*player2_node);
  auto player2_weapon = query_->FindFirst(NodeNameEquals("Weapon"));

  // Assert: Should find different weapon nodes for different players
  ASSERT_TRUE(player1_weapon.has_value());
  ASSERT_TRUE(player2_weapon.has_value());

  // Verify they are different nodes (different handles)
  EXPECT_NE(player1_weapon->GetHandle(), player2_weapon->GetHandle());

  // Both should be named "Weapon" but have different parents
  EXPECT_EQ(player1_weapon->GetName(), "Weapon");
  EXPECT_EQ(player2_weapon->GetName(), "Weapon");
}

//=== Collect Tests ===-----------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithMatchingPredicate_CollectsAllMatches)
{
  using ::testing::AllOf;
  using ::testing::SizeIs;
  using ::testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy
  std::vector<SceneNode> enemies;

  // Act: Collect all enemy nodes
  auto result = query_->Collect(enemies, NodeNameStartsWith("Player"));

  // Assert: Should collect all 2 players
  EXPECT_TRUE(result.completed);
  EXPECT_EQ(result.nodes_matched, 2);

  // Extract node names for comparison
  std::vector<std::string> enemy_names;
  std::transform(enemies.begin(), enemies.end(),
    std::back_inserter(enemy_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers
  EXPECT_THAT(
    enemy_names, AllOf(SizeIs(2), UnorderedElementsAre("Player1", "Player2")));
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithNoMatches_ReturnsEmptyContainer)
{
  // Arrange: Use default game scene hierarchy
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
  using ::testing::SizeIs;

  // Arrange: Use default game scene hierarchy

  // Act: Test different container types
  std::vector<SceneNode> vector_nodes;
  std::deque<SceneNode> deque_nodes;
  std::list<SceneNode> list_nodes;

  auto result1 = query_->Collect(vector_nodes, NodeNameStartsWith("Player"));
  auto result2 = query_->Collect(deque_nodes, NodeNameStartsWith("Player"));
  auto result3 = query_->Collect(list_nodes, NodeNameStartsWith("Player"));

  // Assert: All container types should work
  EXPECT_TRUE(result1.completed);
  EXPECT_TRUE(result2.completed);
  EXPECT_TRUE(result3.completed);

  EXPECT_THAT(vector_nodes, SizeIs(2));
  EXPECT_THAT(deque_nodes, SizeIs(2));
  EXPECT_THAT(list_nodes, SizeIs(2));
}

NOLINT_TEST_F(SceneQueryImmediateTest,
  Collect_WithPreallocatedContainer_PreservesExistingElements)
{
  using ::testing::SizeIs;

  // Arrange: Use default game scene hierarchy

  // Arrange: Create extra node manually and add to container
  auto extra_node = CreateVisibleNode("ExtraNode");
  std::vector<SceneNode> nodes;
  nodes.push_back(extra_node);

  // Act: Collect players into pre-filled container
  auto result = query_->Collect(nodes, NodeNameStartsWith("Player"));

  // Assert: Should preserve existing element and add new ones
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(nodes, SizeIs(3)); // 1 existing + 2 players
  EXPECT_EQ(nodes[0].GetName(), "ExtraNode"); // Original element preserved
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithScopedTraversal_CollectsWithinScope)
{
  using ::testing::Contains;
  using ::testing::Not;
  using ::testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy

  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Collect all nodes in Player1 scope
  std::vector<SceneNode> nodes;
  query_->AddToTraversalScope(*player1_node);
  auto result
    = query_->Collect(nodes, [](const ConstVisitedNode&) { return true; });

  // Assert: Should collect exactly Player1 + its 3 equipment items
  EXPECT_TRUE(result.completed);

  // Extract node names for easy comparison
  std::vector<std::string> node_names;
  std::transform(nodes.begin(), nodes.end(), std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers - exact match
  EXPECT_THAT(
    node_names, UnorderedElementsAre("Player1", "Weapon", "Shield", "Armor"));
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithMultipleScopedNodes_CollectsFromAll)
{
  using ::testing::Contains;
  using ::testing::IsSupersetOf;
  using ::testing::Not;

  // Arrange: Use default game scene hierarchy

  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  auto player2_node = query_->FindFirst(NodeNameEquals("Player2"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(player2_node.has_value());

  // Act: Add multiple nodes to traversal scope
  std::vector<SceneNode> scope_nodes = { *player1_node, *player2_node };
  query_->AddToTraversalScope(scope_nodes);

  std::vector<SceneNode> collected;
  auto result
    = query_->Collect(collected, [](const ConstVisitedNode&) { return true; });

  // Assert: Should collect from both scoped subtrees
  EXPECT_TRUE(result.completed);

  // Extract node names for comparison
  std::vector<std::string> node_names;
  std::transform(collected.begin(), collected.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain nodes from both players
  EXPECT_THAT(node_names, IsSupersetOf({ "Shield", "Armor", "Quiver", "Bow" }));

  // Should not contain nodes outside scope
  EXPECT_THAT(node_names, Not(Contains("Merchant")));
  EXPECT_THAT(node_names, Not(Contains("Tree1")));
}

//=== Count Tests ===-------------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryImmediateTest, Count_WithMatchingPredicate_ReturnsCorrectCount)
{
  // Arrange: Use default game scene hierarchy

  // Act: Count all visible nodes
  auto result = query_->Count(NodeIsVisible());

  // Assert: Should count all visible nodes correctly
  EXPECT_TRUE(result.completed);
  EXPECT_GT(result.nodes_matched, 2);
  EXPECT_GT(result.nodes_examined, result.nodes_matched);
}

NOLINT_TEST_F(SceneQueryImmediateTest, Count_WithNoMatches_ReturnsZero)
{
  // Arrange: Use default game scene hierarchy

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

NOLINT_TEST_F(
  SceneQueryImmediateTest, Count_WithScopedTraversal_CountsWithinScope)
{
  // Arrange: Use default game scene hierarchy

  // Get total count first for comparison
  auto total_count
    = query_->Count([](const ConstVisitedNode&) { return true; });

  // Find Player1 subtree to scope
  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Count all nodes within Player1 scope
  query_->AddToTraversalScope(*player1_node);
  auto scoped_count
    = query_->Count([](const ConstVisitedNode&) { return true; });

  // Assert: Scoped count should be less than total count
  EXPECT_TRUE(scoped_count.completed);
  EXPECT_GT(scoped_count.nodes_matched, 0);
  EXPECT_LT(scoped_count.nodes_matched, total_count.nodes_matched);
  // Should include Player1 + 3 equipment items = 4 nodes
  EXPECT_EQ(scoped_count.nodes_matched, 4);
}

NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithEmptyScope_TraversesFullScene)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::IsSupersetOf;
  using ::testing::SizeIs;

  // Arrange: Use default game scene hierarchy

  // Get baseline count for full scene
  auto full_count = query_->Count([](const ConstVisitedNode&) { return true; });

  // Act: Reset scope to empty (which means full scene traversal)
  query_->ResetTraversalScope();

  // Test all query methods with empty scope (should traverse full scene)
  auto empty_scope_count
    = query_->Count([](const ConstVisitedNode&) { return true; });

  std::vector<SceneNode> collected_nodes;
  auto collect_result = query_->Collect(
    collected_nodes, [](const ConstVisitedNode&) { return true; });
  auto find_result = query_->FindFirst(NodeNameEquals("Player1"));

  auto any_result = query_->Any(NodeNameEquals("GameWorld"));

  // Assert: Empty scope should behave exactly like full scene traversal
  EXPECT_TRUE(empty_scope_count.completed);
  EXPECT_EQ(empty_scope_count.nodes_matched, full_count.nodes_matched);
  EXPECT_EQ(empty_scope_count.nodes_examined, full_count.nodes_examined);

  EXPECT_TRUE(collect_result.completed);

  EXPECT_TRUE(find_result.has_value());
  EXPECT_EQ(find_result->GetName(), "Player1");
  EXPECT_TRUE(any_result.has_value());
  EXPECT_TRUE(any_result.value());

  // Should contain nodes from all parts of the hierarchy
  std::vector<std::string> node_names;
  std::transform(collected_nodes.begin(), collected_nodes.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test container matchers properly
  EXPECT_THAT(node_names,
    AllOf(SizeIs(full_count.nodes_matched),
      IsSupersetOf(
        { "GameWorld", "Player1", "Player2", "NPCs", "Environment" })));
}

//=== Any Tests ===--------------------------------------------------------//

NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithMatchingPredicate_ReturnsTrue)
{
  // Arrange: Use default game scene hierarchy

  // Act: Check if any node is named "Merchant"
  auto result = query_->Any(NodeNameEquals("Merchant"));

  // Assert: Should return true
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.value());
}

NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithNoMatches_ReturnsFalse)
{
  // Arrange: Use default game scene hierarchy

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

NOLINT_TEST_F(
  SceneQueryImmediateTest, Any_WithScopedTraversal_FindsBasedOnScope)
{
  // Arrange: Use default game scene hierarchy

  // Find Player1 and NPCs subtrees
  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  auto npcs_node = query_->FindFirst(NodeNameEquals("NPCs"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(npcs_node.has_value());

  // Act: Check for weapons within Player1 scope (should find)
  query_->AddToTraversalScope(*player1_node);
  auto player1_has_weapon = query_->Any(NodeNameEquals("Weapon"));

  // Reset and check for weapons within NPCs scope (should not find)
  query_->ResetTraversalScope();
  query_->AddToTraversalScope(*npcs_node);
  auto npcs_has_weapon = query_->Any(NodeNameEquals("Weapon"));

  // Assert: Different scopes should give different results
  ASSERT_TRUE(player1_has_weapon.has_value());
  EXPECT_TRUE(player1_has_weapon.value()); // Player1 has weapons

  ASSERT_TRUE(npcs_has_weapon.has_value());
  EXPECT_FALSE(npcs_has_weapon.value()); // NPCs have no weapons
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

NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithSingleNodeScene_WorksCorrectly)
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
