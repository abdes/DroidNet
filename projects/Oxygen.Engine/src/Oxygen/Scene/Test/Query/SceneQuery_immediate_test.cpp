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

  void CreateGameSceneHierarchy()
  {
    const auto json = GetGameSceneJson();
    scene_ = GetFactory().CreateFromJson(json, "GameScene");
    ASSERT_NE(scene_, nullptr);
    CreateQuery();
  }

  std::string GetGameSceneJson()
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
  std::optional<SceneNode> node_result;

  // Act: Find first tree
  auto query_result
    = query_->FindFirst(node_result, NodeNameStartsWith("Tree"));

  // Assert: Should find Tree1 (first in traversal order)
  EXPECT_TRUE(query_result);
  EXPECT_EQ(query_result.nodes_matched, 1U);
  EXPECT_GT(query_result.nodes_examined, 1U);
  ASSERT_TRUE(node_result.has_value());
  EXPECT_EQ(node_result->GetName(), "Tree1");
}

NOLINT_TEST_F(SceneQueryImmediateTest, FindFirst_WithNoMatches_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy
  std::optional<SceneNode> node_result;

  // Act: Search for non-existent node
  auto query_result
    = query_->FindFirst(node_result, NodeNameEquals("NonExistentNode"));

  // Assert: Should return nullopt
  EXPECT_TRUE(query_result);
  EXPECT_EQ(query_result.nodes_matched, 0U);
  EXPECT_EQ(query_result.nodes_examined, scene_->GetNodeCount());
  EXPECT_FALSE(node_result.has_value());
}

NOLINT_TEST_F(SceneQueryImmediateTest, FindFirst_WithRootNode_FindsImmediately)
{
  // Arrange: Use default game scene hierarchy
  std::optional<SceneNode> node_result;

  // Act: Find root node
  auto query_result
    = query_->FindFirst(node_result, NodeNameEquals("GameWorld"));

  // Assert: Should find World immediately
  EXPECT_TRUE(query_result);
  EXPECT_EQ(query_result.nodes_matched, 1U);
  EXPECT_EQ(query_result.nodes_examined, 1U);
  ASSERT_TRUE(node_result.has_value());
  EXPECT_EQ(node_result->GetName(), "GameWorld");
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, FindFirst_WithScopedTraversal_FindsDifferentNodes)
{
  // Arrange: Use default game scene hierarchy
  // Find Player1 and Player2 nodes
  std::optional<SceneNode> player1_node_result;
  std::optional<SceneNode> player2_node_result;
  auto player1_query_result
    = query_->FindFirst(player1_node_result, NodeNameEquals("Player1"));
  auto player2_query_result
    = query_->FindFirst(player2_node_result, NodeNameEquals("Player2"));
  EXPECT_TRUE(player1_query_result);
  EXPECT_TRUE(player2_query_result);
  ASSERT_TRUE(player1_node_result.has_value());
  ASSERT_TRUE(player2_node_result.has_value());

  // Act: Find weapon scoped to Player1
  std::optional<SceneNode> player1_weapon_result;
  query_->AddToTraversalScope(*player1_node_result);
  auto player1_weapon_query_result
    = query_->FindFirst(player1_weapon_result, NodeNameEquals("Weapon"));

  // Reset and scope to Player2
  query_->ResetTraversalScope();
  query_->AddToTraversalScope(*player2_node_result);
  std::optional<SceneNode> player2_weapon_result;
  auto player2_weapon_query_result
    = query_->FindFirst(player2_weapon_result, NodeNameEquals("Weapon"));

  // Assert: Should find different weapon nodes for different players
  EXPECT_TRUE(player1_weapon_query_result);
  EXPECT_TRUE(player2_weapon_query_result);
  ASSERT_TRUE(player1_weapon_result.has_value());
  ASSERT_TRUE(player2_weapon_result.has_value());

  // Verify they are different nodes (different handles)
  EXPECT_NE(
    player1_weapon_result->GetHandle(), player2_weapon_result->GetHandle());

  // Both should be named "Weapon" but have different parents
  EXPECT_EQ(player1_weapon_result->GetName(), "Weapon");
  EXPECT_EQ(player2_weapon_result->GetName(), "Weapon");
}

//=== Collect Tests ===-----------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithMatchingPredicate_CollectsAllMatches)
{
  using testing::AllOf;
  using testing::SizeIs;
  using testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy
  std::vector<SceneNode> nodes_result;

  // Act: Collect all enemy nodes
  auto query_result
    = query_->Collect(nodes_result, NodeNameStartsWith("Player"));

  // Assert: Should collect all 2 players
  EXPECT_TRUE(query_result);
  EXPECT_EQ(query_result.nodes_matched, 2U);
  EXPECT_GT(query_result.nodes_examined, 0U);

  // Extract node names for comparison
  std::vector<std::string> node_names;
  std::transform(nodes_result.begin(), nodes_result.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers
  EXPECT_THAT(
    node_names, AllOf(SizeIs(2), UnorderedElementsAre("Player1", "Player2")));
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithNoMatches_ReturnsEmptyContainer)
{
  // Arrange: Use default game scene hierarchy
  std::vector<SceneNode> nodes_result;

  // Act: Collect non-existent nodes
  auto query_result
    = query_->Collect(nodes_result, NodeNameEquals("NonExistent"));

  // Assert: Should return empty container
  EXPECT_TRUE(query_result);
  EXPECT_TRUE(nodes_result.empty());
  EXPECT_EQ(query_result.nodes_matched, 0U);
  EXPECT_GT(query_result.nodes_examined, 0U);
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithDifferentContainerTypes_WorksCorrectly)
{
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy

  // Act: Test different container types
  std::vector<SceneNode> vector_nodes_result;
  std::deque<SceneNode> deque_nodes_result;
  std::list<SceneNode> list_nodes_result;

  auto query_result1
    = query_->Collect(vector_nodes_result, NodeNameStartsWith("Player"));
  auto query_result2
    = query_->Collect(deque_nodes_result, NodeNameStartsWith("Player"));
  auto query_result3
    = query_->Collect(list_nodes_result, NodeNameStartsWith("Player"));

  // Assert: All container types should work
  EXPECT_TRUE(query_result1);
  EXPECT_TRUE(query_result2);
  EXPECT_TRUE(query_result3);

  EXPECT_THAT(vector_nodes_result, SizeIs(2));
  EXPECT_THAT(deque_nodes_result, SizeIs(2));
  EXPECT_THAT(list_nodes_result, SizeIs(2));
}

NOLINT_TEST_F(SceneQueryImmediateTest,
  Collect_WithPreallocatedContainer_PreservesExistingElements)
{
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  // Arrange: Create extra node manually and add to container
  auto extra_node = CreateVisibleNode("ExtraNode");
  std::vector<SceneNode> nodes_result;
  nodes_result.push_back(extra_node);

  // Act: Collect players into pre-filled container
  auto query_result
    = query_->Collect(nodes_result, NodeNameStartsWith("Player"));

  // Assert: Should preserve existing element and add new ones
  EXPECT_TRUE(query_result);
  EXPECT_EQ(query_result.nodes_matched, 2U);
  EXPECT_THAT(nodes_result, SizeIs(3)); // 1 existing + 2 players
  EXPECT_EQ(
    nodes_result[0].GetName(), "ExtraNode"); // Original element preserved
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithScopedTraversal_CollectsWithinScope)
{
  using testing::Contains;
  using testing::Not;
  using testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy
  std::optional<SceneNode> player1_node_result;
  auto player1_query_result
    = query_->FindFirst(player1_node_result, NodeNameEquals("Player1"));
  EXPECT_TRUE(player1_query_result);
  ASSERT_TRUE(player1_node_result.has_value());

  // Act: Collect all nodes in Player1 scope
  std::vector<SceneNode> nodes_result;
  query_->AddToTraversalScope(*player1_node_result);
  auto collect_query_result = query_->Collect(
    nodes_result, [](const ConstVisitedNode&) { return true; });

  // Assert: Should collect exactly Player1 + its 3 equipment items
  EXPECT_TRUE(collect_query_result);
  EXPECT_EQ(collect_query_result.nodes_matched, 4U);

  // Extract node names for easy comparison
  std::vector<std::string> node_names;
  std::transform(nodes_result.begin(), nodes_result.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers - exact match
  EXPECT_THAT(
    node_names, UnorderedElementsAre("Player1", "Weapon", "Shield", "Armor"));
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Collect_WithMultipleScopedNodes_CollectsFromAll)
{
  using testing::Contains;
  using testing::IsSupersetOf;
  using testing::Not;

  // Arrange: Use default game scene hierarchy
  std::optional<SceneNode> player1_node_result;
  std::optional<SceneNode> player2_node_result;
  auto player1_query_result
    = query_->FindFirst(player1_node_result, NodeNameEquals("Player1"));
  auto player2_query_result
    = query_->FindFirst(player2_node_result, NodeNameEquals("Player2"));
  EXPECT_TRUE(player1_query_result);
  EXPECT_TRUE(player2_query_result);
  ASSERT_TRUE(player1_node_result.has_value());
  ASSERT_TRUE(player2_node_result.has_value());

  // Act: Add multiple nodes to traversal scope
  std::vector<SceneNode> scope_nodes
    = { *player1_node_result, *player2_node_result };
  query_->AddToTraversalScope(scope_nodes);

  std::vector<SceneNode> nodes_result;
  auto collect_query_result = query_->Collect(
    nodes_result, [](const ConstVisitedNode&) { return true; });

  // Assert: Should collect from both scoped subtrees
  EXPECT_TRUE(collect_query_result);
  EXPECT_GT(collect_query_result.nodes_matched, 0U);

  // Extract node names for comparison
  std::vector<std::string> node_names;
  std::transform(nodes_result.begin(), nodes_result.end(),
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
  std::optional<size_t> count_result;
  auto query_result = query_->Count(count_result, NodeIsVisible());

  // Assert: Should count all visible nodes correctly
  EXPECT_TRUE(query_result);
  EXPECT_GT(query_result.nodes_examined, 0U);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_GT(count_result.value(), 2U);
  EXPECT_GT(query_result.nodes_examined, query_result.nodes_matched);
}

NOLINT_TEST_F(SceneQueryImmediateTest, Count_WithNoMatches_ReturnsZero)
{
  // Arrange: Use default game scene hierarchy

  // Act: Count non-existent nodes
  std::optional<size_t> count_result;
  auto query_result
    = query_->Count(count_result, NodeNameEquals("NonExistent"));

  // Assert: Should return zero count
  EXPECT_TRUE(query_result);
  EXPECT_GT(query_result.nodes_examined, 0U);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_EQ(count_result.value(), 0U);
  EXPECT_EQ(query_result.nodes_matched, 0U);
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Count_WithLargeHierarchy_CountsEfficiently)
{
  // Arrange: Create large forest using TestSceneFactory
  CreateForestScene(5, 10); // 5 roots with 10 children each = 55+ nodes

  // Act: Count all nodes
  std::optional<size_t> count_result;
  auto query_result
    = query_->Count(count_result, [](const ConstVisitedNode&) { return true; });

  // Assert: Should count all nodes efficiently
  EXPECT_TRUE(query_result);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_EQ(count_result.value(), 55U);
  EXPECT_EQ(query_result.nodes_matched, query_result.nodes_examined);
  EXPECT_EQ(query_result.nodes_matched, 55U); // At least 5 roots + 50 children
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Count_WithScopedTraversal_CountsWithinScope)
{
  // Arrange: Use default game scene hierarchy
  // Get total count first for comparison
  std::optional<size_t> total_count_result;
  auto total_query_result = query_->Count(
    total_count_result, [](const ConstVisitedNode&) { return true; });

  // Find Player1 subtree to scope
  std::optional<SceneNode> player1_node_result;
  auto player1_query_result
    = query_->FindFirst(player1_node_result, NodeNameEquals("Player1"));
  EXPECT_TRUE(player1_query_result);
  ASSERT_TRUE(player1_node_result.has_value());

  // Act: Count all nodes within Player1 scope
  query_->AddToTraversalScope(*player1_node_result);
  std::optional<size_t> scoped_count_result;
  auto scoped_query_result = query_->Count(
    scoped_count_result, [](const ConstVisitedNode&) { return true; });

  // Assert: Scoped count should be less than total count
  EXPECT_TRUE(total_query_result);
  EXPECT_TRUE(scoped_query_result);
  ASSERT_TRUE(total_count_result.has_value());
  ASSERT_TRUE(scoped_count_result.has_value());
  EXPECT_EQ(scoped_count_result.value(), 4U); // Player1 + 3 items
  EXPECT_LT(scoped_count_result.value(), total_count_result.value());
}

NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithEmptyScope_TraversesFullScene)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::IsSupersetOf;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  // Get baseline count for full scene
  std::optional<size_t> full_count_result;
  auto full_query_result = query_->Count(
    full_count_result, [](const ConstVisitedNode&) { return true; });

  // Act: Reset scope to empty (which means full scene traversal)
  query_->ResetTraversalScope();

  // Test all query methods with empty scope (should traverse full scene)
  std::optional<size_t> empty_scope_count_result;
  auto empty_scope_query_result = query_->Count(
    empty_scope_count_result, [](const ConstVisitedNode&) { return true; });

  std::vector<SceneNode> collected_nodes_result;
  auto collect_query_result = query_->Collect(
    collected_nodes_result, [](const ConstVisitedNode&) { return true; });

  std::optional<SceneNode> find_node_result;
  auto find_query_result
    = query_->FindFirst(find_node_result, NodeNameEquals("Player1"));

  std::optional<bool> any_result;
  auto any_query_result = query_->Any(any_result, NodeNameEquals("GameWorld"));

  // Assert: Empty scope should behave exactly like full scene traversal
  EXPECT_TRUE(full_query_result);
  EXPECT_TRUE(empty_scope_query_result);
  ASSERT_TRUE(full_count_result.has_value());
  ASSERT_TRUE(empty_scope_count_result.has_value());
  EXPECT_EQ(empty_scope_count_result.value(), full_count_result.value());

  EXPECT_TRUE(collect_query_result);
  EXPECT_TRUE(find_query_result);
  EXPECT_TRUE(any_query_result);

  ASSERT_TRUE(find_node_result.has_value());
  EXPECT_EQ(find_node_result->GetName(), "Player1");
  ASSERT_TRUE(any_result.has_value());
  EXPECT_TRUE(any_result.value());

  // Should contain nodes from all parts of the hierarchy
  std::vector<std::string> node_names;
  std::transform(collected_nodes_result.begin(), collected_nodes_result.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test container matchers properly
  EXPECT_THAT(node_names,
    AllOf(SizeIs(full_count_result.value()),
      IsSupersetOf(
        { "GameWorld", "Player1", "Player2", "NPCs", "Environment" })));
}

//=== Any Tests ===--------------------------------------------------------//

NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithMatchingPredicate_ReturnsTrue)
{
  // Arrange: Use default game scene hierarchy

  // Act: Check if any node is named "Merchant"
  std::optional<bool> any_result;
  auto query_result = query_->Any(any_result, NodeNameEquals("Merchant"));

  // Assert: Should return true
  EXPECT_TRUE(query_result);
  EXPECT_GT(query_result.nodes_examined, 0U);
  ASSERT_TRUE(any_result.has_value());
  EXPECT_TRUE(any_result.value());
}

NOLINT_TEST_F(SceneQueryImmediateTest, Any_WithNoMatches_ReturnsFalse)
{
  // Arrange: Use default game scene hierarchy

  // Act: Check for non-existent node
  std::optional<bool> any_result;
  auto query_result = query_->Any(any_result, NodeNameEquals("NonExistent"));

  // Assert: Should return false
  EXPECT_TRUE(query_result);
  EXPECT_GT(query_result.nodes_examined, 0U);
  ASSERT_TRUE(any_result.has_value());
  EXPECT_FALSE(any_result.value());
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Any_WithFirstNodeMatching_ReturnsImmediately)
{
  // Arrange: Create linear chain where root matches
  CreateLinearChainScene(10); // Deep chain for early termination test

  // Act: Search for root node (should terminate immediately)
  std::optional<bool> any_result;
  auto query_result = query_->Any(any_result, NodeNameEquals("Root"));

  // Assert: Should return true immediately
  EXPECT_TRUE(query_result);
  EXPECT_GT(query_result.nodes_examined, 0U);
  ASSERT_TRUE(any_result.has_value());
  EXPECT_TRUE(any_result.value());
}

NOLINT_TEST_F(
  SceneQueryImmediateTest, Any_WithScopedTraversal_FindsBasedOnScope)
{
  // Arrange: Use default game scene hierarchy
  // Find Player1 and NPCs subtrees
  std::optional<SceneNode> player1_node;
  std::optional<SceneNode> npcs_node;
  query_->FindFirst(player1_node, NodeNameEquals("Player1"));
  query_->FindFirst(npcs_node, NodeNameEquals("NPCs"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(npcs_node.has_value()); // Act: Check for weapons within Player1
                                      // scope (should find)
  query_->AddToTraversalScope(*player1_node);
  std::optional<bool> player1_any_result;
  auto player1_query_result
    = query_->Any(player1_any_result, NodeNameEquals("Weapon"));

  // Reset and check for weapons within NPCs scope (should not find)
  query_->ResetTraversalScope();
  query_->AddToTraversalScope(*npcs_node);
  std::optional<bool> npcs_any_result;
  auto npcs_query_result
    = query_->Any(npcs_any_result, NodeNameEquals("Weapon"));

  // Assert: Different scopes should give different results
  EXPECT_TRUE(player1_query_result);
  ASSERT_TRUE(player1_any_result.has_value());
  EXPECT_TRUE(player1_any_result.value()); // Player1 has weapons

  EXPECT_TRUE(npcs_query_result);
  ASSERT_TRUE(npcs_any_result.has_value());
  EXPECT_FALSE(npcs_any_result.value()); // NPCs have no weapons
}

//=== Edge Cases and Error Conditions ===----------------------------------//

NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithEmptyScene_HandlesGracefully)
{
  // Arrange: Create empty scene
  scene_->Clear();
  ASSERT_TRUE(scene_->IsEmpty());
  CreateQuery();

  // Act: Perform various queries on empty scene
  std::optional<SceneNode> find_node_result;
  auto find_query_result
    = query_->FindFirst(find_node_result, NodeNameEquals("Any"));

  std::optional<bool> any_result;
  auto any_query_result = query_->Any(any_result, NodeNameEquals("Any"));

  std::optional<size_t> count_result;
  auto count_query_result = query_->Count(count_result, NodeNameEquals("Any"));

  std::vector<SceneNode> nodes_result;
  auto collect_query_result
    = query_->Collect(nodes_result, NodeNameEquals("Any"));

  // Assert: All operations should complete gracefully
  EXPECT_TRUE(find_query_result);
  EXPECT_FALSE(find_node_result.has_value());

  EXPECT_TRUE(any_query_result);
  ASSERT_TRUE(any_result.has_value());
  EXPECT_FALSE(any_result.value());

  EXPECT_TRUE(count_query_result);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_EQ(count_result.value(), 0U);

  EXPECT_TRUE(collect_query_result);
  EXPECT_TRUE(nodes_result.empty());
}

NOLINT_TEST_F(SceneQueryImmediateTest, Query_WithSingleNodeScene_WorksCorrectly)
{
  // Arrange: Create simple single node scene
  CreateSimpleScene();

  // Act: Query the single node
  std::optional<SceneNode> find_node_result;
  auto find_query_result
    = query_->FindFirst(find_node_result, NodeNameEquals("Root"));

  std::optional<size_t> count_result;
  auto count_query_result
    = query_->Count(count_result, [](const ConstVisitedNode&) { return true; });

  // Assert: Should find the single node
  EXPECT_TRUE(find_query_result);
  ASSERT_TRUE(find_node_result.has_value());
  EXPECT_EQ(find_node_result->GetName(), "Root");

  EXPECT_TRUE(count_query_result);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_EQ(count_result.value(), 1U);
}

} // namespace
