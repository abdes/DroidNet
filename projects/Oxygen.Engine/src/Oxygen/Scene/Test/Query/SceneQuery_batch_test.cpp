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
  auto SetUp() -> void override
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
  std::optional<SceneNode> player; // Act: Execute single FindFirst in batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    // Single query to find player node
    q.BatchFindFirst(player, NodeNameEquals("Player1"));
  });

  // Assert: Should execute correctly and find player
  EXPECT_TRUE(batch_result);
  EXPECT_GT(batch_result.nodes_examined, 0);
  EXPECT_GT(batch_result.total_matches, 0);

  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->GetName(), "Player1");
}

NOLINT_TEST_F(SceneQueryBatchTest,
  ExecuteBatch_WithMultipleQueries_ExecutesInSingleTraversal)
{ // Arrange: Multiple operations to batch
  std::optional<SceneNode> player;
  std::vector<SceneNode> enemies;
  std::optional<size_t> enemies_count;
  std::optional<bool> has_ui; // Act: Execute multiple queries in single batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchFindFirst(player, NodeNameEquals("Player1"));
    q.BatchCollect(enemies, NodeNameStartsWith("Enemy"));
    q.BatchCount(enemies_count, NodeNameStartsWith("Enemy"));
    q.BatchAny(has_ui, NodeNameEquals("UI"));
  });

  // Assert: All operations should complete in single traversal
  EXPECT_TRUE(batch_result);
  EXPECT_GT(batch_result.nodes_examined, 0);
  EXPECT_GT(batch_result.total_matches, 0);
  EXPECT_EQ(batch_result.operation_results.size(), 4); // 4 operations
  // Verify individual results from reference variables
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->GetName(), "Player1");

  EXPECT_EQ(enemies.size(), 3); // Enemy1, Enemy2, Enemy3

  ASSERT_TRUE(has_ui.has_value());
  EXPECT_TRUE(has_ui.value());
  // Verify operation metadata is available in batch_result.operation_results
  EXPECT_TRUE(batch_result.operation_results[0]);
  EXPECT_TRUE(batch_result.operation_results[1]);
  EXPECT_TRUE(batch_result.operation_results[2]);
  EXPECT_TRUE(batch_result.operation_results[3]);
}

NOLINT_TEST_F(SceneQueryBatchTest,
  ExecuteBatch_WithMixedOperations_AggregatesResultsCorrectly)
{
  // Arrange: Mix of FindFirst, Collect, Count, Any operations
  std::optional<SceneNode> level1;
  std::vector<SceneNode> potions;
  std::vector<SceneNode> ui_elements;
  std::optional<bool> has_static;
  std::optional<size_t> visible_count;

  // Act: Execute mixed batch operations
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchFindFirst(level1, NodeNameEquals("Level1"));
    q.BatchCollect(potions, NodeNameStartsWith("Potion"));
    q.BatchCollect(ui_elements, [](const ConstVisitedNode& visited) {
      if (!visited.node_impl)
        return false;
      auto parent = visited.node_impl->AsGraphNode().GetParent();
      return parent.IsValid(); // Has parent check
    });
    q.BatchCount(visible_count, NodeIsVisible());
    q.BatchAny(has_static, NodeIsStatic());
  });
  // Assert: Batch should aggregate all results correctly
  EXPECT_TRUE(batch_result);
  EXPECT_EQ(batch_result.operation_results.size(), 5); // 5 operations
  // Check operation metadata from batch_result
  auto visible_count_op = batch_result.operation_results[3]; // Count operation
  EXPECT_TRUE(visible_count_op);
  EXPECT_TRUE(visible_count.has_value());

  // Total matches should be sum of all individual matches from user variables
  std::size_t expected_total = 0;
  if (level1.has_value())
    expected_total += 1;
  expected_total += potions.size();
  expected_total += ui_elements.size();
  expected_total
    += visible_count_op.nodes_matched; // Use nodes_matched for count
  if (has_static.has_value() && has_static.value())
    expected_total += 1;

  EXPECT_EQ(batch_result.total_matches, expected_total);
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithEarlyTermination_StopsWhenAllComplete)
{
  // Arrange: Operations that should terminate early
  std::optional<SceneNode> first_node;
  std::optional<bool>
    any_node; // Act: Execute batch with early termination operations
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchFindFirst(first_node, [](const ConstVisitedNode&) {
      return true;
    }); // Should find first immediately
    q.BatchAny(any_node, [](const ConstVisitedNode&) {
      return true;
    }); // Should find any immediately
  });

  // Assert: Should complete with early termination
  EXPECT_TRUE(batch_result);
  EXPECT_GT(batch_result.nodes_examined, 0);

  ASSERT_TRUE(first_node.has_value());
  ASSERT_TRUE(any_node.has_value());
  EXPECT_TRUE(any_node.value());
}

NOLINT_TEST_F(SceneQueryBatchTest, ExecuteBatch_WithLargeHierarchy_ScalesWell)
{
  // Arrange: Create very large hierarchy
  CreateForestScene(50, 20); // 50 roots with 20 children = 1000+ nodes
  CreateQuery();
  std::vector<SceneNode> all_nodes;
  std::vector<SceneNode> root_nodes;
  std::optional<size_t> count;

  // Act: Execute complex batch on large hierarchy
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchCollect(all_nodes, [](const ConstVisitedNode&) { return true; });
    q.BatchCollect(root_nodes, NodeNameStartsWith("Root"));
    q.BatchCount(count, [](const ConstVisitedNode&) { return true; });
  });
  // Assert: Should handle large hierarchy efficiently
  EXPECT_TRUE(batch_result);
  EXPECT_GT(batch_result.nodes_examined, 1000);
  EXPECT_GT(all_nodes.size(), 1000);
  EXPECT_EQ(root_nodes.size(), 50);
  // Get count operation metadata from batch_result
  auto total_count_op = batch_result.operation_results[2]; // Count operation
  EXPECT_EQ(
    total_count_op.nodes_matched, all_nodes.size()); // Compare nodes_matched
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_ResultAggregation_CalculatesCorrectly)
{
  // Arrange: Specific operations with known results
  std::vector<SceneNode> enemies;
  std::vector<SceneNode> potions;
  std::optional<SceneNode> player;
  std::optional<SceneNode> ui; // Act: Execute batch with predictable results
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchCollect(enemies, NodeNameStartsWith("Enemy")); // Should find 3
    q.BatchCollect(potions, NodeNameStartsWith("Potion")); // Should find 2
    q.BatchFindFirst(player, NodeNameEquals("Player1")); // Should find 1
    q.BatchFindFirst(ui, NodeNameEquals("UI")); // Should find 1
  });

  // Assert: Aggregation should be correct
  EXPECT_TRUE(batch_result);

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
  EXPECT_TRUE(batch_result);
  EXPECT_EQ(batch_result.total_matches, 0);
  EXPECT_GE(batch_result.nodes_examined, 0); // May still examine some nodes
}

NOLINT_TEST_F(
  SceneQueryBatchTest, ExecuteBatch_WithOnlyCountOperations_WorksCorrectly)
{
  // Arrange: Batch with only count operations
  std::optional<size_t> all_count;
  std::optional<size_t> enemies_count;
  std::optional<size_t> visible_count;

  // Act: Execute count-only batch
  auto batch_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchCount(all_count, [](const ConstVisitedNode&) { return true; });
    q.BatchCount(enemies_count, NodeNameStartsWith("Enemy"));
    q.BatchCount(visible_count, NodeIsVisible());
  });
  // Assert: Should work correctly
  EXPECT_TRUE(batch_result);
  EXPECT_EQ(batch_result.operation_results.size(), 3); // 3 count operations
  // Check individual count operation metadata from batch_result
  auto all_count_op = batch_result.operation_results[0];
  auto enemy_count_op = batch_result.operation_results[1];
  auto visible_count_op = batch_result.operation_results[2];

  EXPECT_GT(all_count_op.nodes_matched, 0);
  EXPECT_EQ(enemy_count_op.nodes_matched, 3);

  // Total matches should be sum of all counts
  EXPECT_EQ(batch_result.total_matches,
    all_count_op.nodes_matched + enemy_count_op.nodes_matched
      + visible_count_op.nodes_matched);
}

NOLINT_TEST_F(SceneQueryBatchTest, ExecuteBatch_WithNestedBatch_AbortWithDCHECK)
{
  // Arrange: Attempt nested batch (should trigger DCHECK_F and abort)
  std::optional<SceneNode> outer_result;

  // Act & Assert: Nested batch should trigger DCHECK_F and abort
  EXPECT_DEATH(
    {
      auto batch_result = query_->ExecuteBatch([&](const auto& q) {
        q.BatchFindFirst(outer_result, NodeNameEquals("Player1"));

        // This nested batch should trigger DCHECK_F
        auto nested_result = q.ExecuteBatch([&](const auto& nested_q) {
          std::optional<SceneNode> inner;
          nested_q.BatchFindFirst(inner, NodeNameEquals("Enemy1"));
        });
      });
    },
    ".*"); // Match any death message from DCHECK_F
}

} // namespace
