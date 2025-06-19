//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "SceneQueryTestBase.h"

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::QueryResult;
using oxygen::scene::SceneNode;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Batch Processing Test Fixture ===-------------------------------------//

class SceneQueryBatchTest : public SceneQueryTestBase {
protected:
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
          "name": "GameScene"
        },
        "nodes": [
          {
            "name": "Level1",
            "children": [
              {
                "name": "Player1",
                "flags": {"visible": true, "static": false},
                "children": [
                  {"name": "Weapon"},
                  {"name": "Shield"}
                ]
              },
              {
                "name": "Player2",
                "flags": {"visible": true, "static": false},
                "children": [
                  {"name": "Bow"},
                  {"name": "Quiver"}
                ]
              },
              {
                "name": "Enemies",
                "children": [
                  {"name": "Enemy1", "flags": {"visible": true}},
                  {"name": "Enemy2", "flags": {"visible": false}},
                  {"name": "Enemy3", "flags": {"visible": true}}
                ]
              },
              {
                "name": "Items",
                "children": [
                  {"name": "Potion1"},
                  {"name": "Potion2"},
                  {"name": "Key"}
                ]
              }
            ]
          },
          {
            "name": "UI",
            "flags": {"static": true},
            "children": [
              {"name": "MainMenu"},
              {"name": "HealthBar"},
              {"name": "Inventory"}
            ]
          }
        ]
      })";
  }
};

//=== Basic Batch Tests ===------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithSingleQuery_ExecutesCorrectly)
{
  // Arrange: Single query in batch mode
  std::optional<SceneNode> player;

  // Act: Execute single FindFirst in batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    // Single query to find player node
    player = q.FindFirst(NodeNameEquals("Player1"));
  });

  // Assert: Should execute correctly and find player
  EXPECT_TRUE(batch_result.completed);
  EXPECT_GT(batch_result.nodes_examined, 0);
  EXPECT_GT(batch_result.total_matches, 0);

  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->GetName(), "Player1");
}

NOLINT_TEST_F(SceneQueryBatchTest,
  ExecuteBatch_WithMultipleQueries_ExecutesInSingleTraversal)
{
  // Arrange: Multiple operations to batch
  std::optional<SceneNode> player;
  std::vector<SceneNode> enemies;
  QueryResult enemy_count_result;
  std::optional<bool> has_ui;

  // Act: Execute multiple queries in single batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    player = q.FindFirst(NodeNameEquals("Player"));
    q.Collect(enemies, NodeNameStartsWith("Enemy"));
    enemy_count_result = q.Count(NodeNameStartsWith("Enemy"));
    has_ui = q.Any(NodeNameEquals("UI"));
  });

  // Assert: All operations should complete in single traversal
  EXPECT_TRUE(batch_result.completed);
  EXPECT_GT(batch_result.nodes_examined, 0);
  EXPECT_GT(batch_result.total_matches, 0);

  // Verify individual results
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->GetName(), "Player");

  EXPECT_EQ(enemies.size(), 3); // Enemy1, Enemy2, Enemy3
  EXPECT_EQ(enemy_count_result.nodes_matched, 3);

  ASSERT_TRUE(has_ui.has_value());
  EXPECT_TRUE(has_ui.value());
}

NOLINT_TEST_F(SceneQueryBatchTest,
  ExecuteBatch_WithMixedOperations_AggregatesResultsCorrectly)
{
  // Arrange: Mix of FindFirst, Collect, Count, Any operations
  std::optional<SceneNode> level1;
  std::vector<SceneNode> potions;
  std::vector<SceneNode> ui_elements;
  QueryResult visible_count;
  std::optional<bool> has_static;

  // Act: Execute mixed batch operations
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    level1 = q.FindFirst(NodeNameEquals("Level1"));
    q.Collect(potions, NodeNameStartsWith("Potion"));
    q.Collect(ui_elements, [](const ConstVisitedNode& visited) {
      if (!visited.node_impl)
        return false;
      auto parent = visited.node_impl->AsGraphNode().GetParent();
      return parent.IsValid(); // Has parent check
    });
    visible_count = q.Count(NodeIsVisible());
    has_static = q.Any(NodeIsStatic());
  });

  // Assert: Batch should aggregate all results correctly
  EXPECT_TRUE(batch_result.completed);

  // Total matches should be sum of all individual matches
  std::size_t expected_total = 0;
  if (level1.has_value())
    expected_total += 1;
  expected_total += potions.size();
  expected_total += ui_elements.size();
  expected_total += visible_count.nodes_matched;
  if (has_static.has_value() && has_static.value())
    expected_total += 1;

  EXPECT_EQ(batch_result.total_matches, expected_total);
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithEarlyTermination_StopsWhenAllComplete)
{
  // Arrange: Operations that should terminate early
  std::optional<SceneNode> first_node;
  std::optional<bool> any_node;

  // Act: Execute batch with early termination operations
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    first_node = q.FindFirst([](const ConstVisitedNode&) {
      return true;
    }); // Should find first immediately
    any_node = q.Any([](const ConstVisitedNode&) {
      return true;
    }); // Should find any immediately
  });

  // Assert: Should complete with early termination
  EXPECT_TRUE(batch_result.completed);
  EXPECT_GT(batch_result.nodes_examined, 0);

  ASSERT_TRUE(first_node.has_value());
  ASSERT_TRUE(any_node.has_value());
  EXPECT_TRUE(any_node.value());
}

//=== Batch Performance Tests ==============================================//

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_VsIndividualQueries_PerformanceBenefit)
{
  // Arrange: Create larger hierarchy for performance testing
  CreateForestScene(20, 10); // 20 roots with 10 children each = 220+ nodes
  CreateQuery();

  // Measure individual queries (simulated with separate calls)
  std::optional<SceneNode> player1, player2;
  std::vector<SceneNode> roots1, roots2;
  QueryResult count1, count2;

  // Act: Batch execution
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    player1 = q.FindFirst(NodeNameEquals("Root"));
    q.Collect(roots1, NodeNameStartsWith("Root"));
    count1 = q.Count([](const ConstVisitedNode&) { return true; });
  });

  // Individual executions (for comparison)
  player2 = query_->FindFirst(NodeNameEquals("Root"));
  auto collect_result = query_->Collect(roots2, NodeNameStartsWith("Root"));
  count2 = query_->Count([](const ConstVisitedNode&) { return true; });

  // Assert: Batch should examine fewer or equal nodes due to single traversal
  EXPECT_TRUE(batch_result.completed);
  EXPECT_LE(batch_result.nodes_examined,
    count1.nodes_examined + collect_result.nodes_examined
      + count2.nodes_examined);

  // Results should be equivalent
  EXPECT_EQ(player1.has_value(), player2.has_value());
  if (player1.has_value() && player2.has_value()) {
    EXPECT_EQ(player1->GetName(), player2->GetName());
  }
  EXPECT_EQ(roots1.size(), roots2.size());
  EXPECT_EQ(count1.nodes_matched, count2.nodes_matched);
}

NOLINT_TEST_F(SceneQueryBatchTest, ExecuteBatch_WithLargeHierarchy_ScalesWell)
{
  // Arrange: Create very large hierarchy
  CreateForestScene(50, 20); // 50 roots with 20 children = 1000+ nodes
  CreateQuery();

  std::vector<SceneNode> all_nodes;
  std::vector<SceneNode> root_nodes;
  QueryResult total_count;

  // Act: Execute complex batch on large hierarchy
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.Collect(all_nodes, [](const ConstVisitedNode&) { return true; });
    q.Collect(root_nodes, NodeNameStartsWith("Root"));
    total_count = q.Count([](const ConstVisitedNode&) { return true; });
  });

  // Assert: Should handle large hierarchy efficiently
  EXPECT_TRUE(batch_result.completed);
  EXPECT_GT(batch_result.nodes_examined, 1000);
  EXPECT_GT(all_nodes.size(), 1000);
  EXPECT_EQ(root_nodes.size(), 50);
  EXPECT_EQ(total_count.nodes_matched, all_nodes.size());
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_ResultAggregation_CalculatesCorrectly)
{
  // Arrange: Specific operations with known results
  std::vector<SceneNode> enemies;
  std::vector<SceneNode> potions;
  std::optional<SceneNode> player;
  std::optional<SceneNode> ui;

  // Act: Execute batch with predictable results
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.Collect(enemies, NodeNameStartsWith("Enemy")); // Should find 3
    q.Collect(potions, NodeNameStartsWith("Potion")); // Should find 2
    player = q.FindFirst(NodeNameEquals("Player")); // Should find 1
    ui = q.FindFirst(NodeNameEquals("UI")); // Should find 1
  });

  // Assert: Aggregation should be correct
  EXPECT_TRUE(batch_result.completed);

  std::size_t expected_total = enemies.size() + potions.size();
  if (player.has_value())
    expected_total += 1;
  if (ui.has_value())
    expected_total += 1;

  EXPECT_EQ(batch_result.total_matches, expected_total);
  EXPECT_EQ(enemies.size(), 3);
  EXPECT_EQ(potions.size(), 2);
  ASSERT_TRUE(player.has_value());
  ASSERT_TRUE(ui.has_value());
}

//=== Batch Edge Cases and Error Handling ==================================//

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithEmptyBatch_CompletesSuccessfully)
{
  // Arrange: Empty batch operation

  // Act: Execute empty batch
  auto batch_result = query_->ExecuteBatch([&](const auto& /*q*/) {
    // No operations
  });

  // Assert: Should complete successfully with zero results
  EXPECT_TRUE(batch_result.completed);
  EXPECT_EQ(batch_result.total_matches, 0);
  EXPECT_GE(batch_result.nodes_examined, 0); // May still examine some nodes
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithOnlyCountOperations_WorksCorrectly)
{
  // Arrange: Batch with only count operations
  QueryResult all_count, enemy_count, visible_count;

  // Act: Execute count-only batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    all_count = q.Count([](const ConstVisitedNode&) { return true; });
    enemy_count = q.Count(NodeNameStartsWith("Enemy"));
    visible_count = q.Count(NodeIsVisible());
  });

  // Assert: Should work correctly
  EXPECT_TRUE(batch_result.completed);
  EXPECT_GT(all_count.nodes_matched, 0);
  EXPECT_EQ(enemy_count.nodes_matched, 3);

  // Total matches should be sum of all counts
  EXPECT_EQ(batch_result.total_matches,
    all_count.nodes_matched + enemy_count.nodes_matched
      + visible_count.nodes_matched);
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithNestedBatch_HandlesGracefully)
{
  // Arrange: Attempt nested batch (should be handled gracefully)
  std::optional<SceneNode> outer_result;

  // Act: Try to execute nested batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    outer_result = q.FindFirst(NodeNameEquals("Player"));

    // This nested batch should be handled gracefully
    auto nested_result = q.ExecuteBatch([&](const auto& nested_q) {
      auto inner = nested_q.FindFirst(NodeNameEquals("Enemy1"));
      // Nested operation
    });

    // Continue with outer batch
    EXPECT_TRUE(nested_result.completed);
  });

  // Assert: Should handle nested batch gracefully
  EXPECT_TRUE(batch_result.completed);
  ASSERT_TRUE(outer_result.has_value());
  EXPECT_EQ(outer_result->GetName(), "Player");
}

//=== Path Operations in Batch Mode =======================================//

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithPathOperations_ReturnsIncomplete)
{
  // Arrange: Try to use path operations in batch mode
  std::optional<SceneNode> path_result;
  std::vector<SceneNode> path_collection;

  // Act: Attempt path operations in batch (should fail gracefully)
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    path_result = q.FindFirstByPath("Level1/Player");
    auto collect_result = q.CollectByPath(path_collection, "**/Enemy*");

    // These should return incomplete results but not crash
    EXPECT_FALSE(path_result.has_value());
    EXPECT_FALSE(collect_result.completed);
  });

  // Assert: Batch should indicate path operations aren't supported
  // The batch itself may complete, but path operations should fail
  EXPECT_FALSE(path_result.has_value());
  EXPECT_TRUE(path_collection.empty());
}

} // namespace
