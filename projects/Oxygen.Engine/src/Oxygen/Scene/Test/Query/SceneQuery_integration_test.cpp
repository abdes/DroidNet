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
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Integration Test Fixture ===============================================//

class SceneQueryIntegrationTest : public SceneQueryTestBase {
protected:
  void SetUp() override { }

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
          "name": "GameScene"
        },
        "nodes": [
          {
            "name": "Level1",
            "children": [
              {
                "name": "Player",
                "flags": {"visible": true, "static": false},
                "children": [
                  {"name": "Weapon"},
                  {"name": "Shield"}
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

//=== Complex Hierarchy Tests ================================================//

NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_WithDeepHierarchy_TraversesCorrectly)
{
  // Arrange: Create deep nested hierarchy using JSON
  const auto deep_json = R"({
      "metadata": {
        "name": "DeepHierarchy"
      },
      "nodes": [
        {
          "name": "L0",
          "children": [
            {
              "name": "L1",
              "children": [
                {
                  "name": "L2",
                  "children": [
                    {
                      "name": "L3",
                      "children": [
                        {
                          "name": "L4",
                          "children": [
                            {"name": "L5_Target"}
                          ]
                        }
                      ]
                    }
                  ]
                }
              ]
            }
          ]
        }
      ]
    })";
  scene_ = GetFactory().CreateFromJson(deep_json, "DeepHierarchy");
  CreateQuery();

  // Act: Find deeply nested target
  std::optional<SceneNode> node_result;
  auto find_query_result
    = query_->FindFirst(node_result, NodeNameEquals("L5_Target"));

  // Count nodes at each level
  std::vector<SceneNode> level_nodes_result;
  for (int i = 0; i <= 5; ++i) {
    std::string level_name = (i == 5) ? "L5_Target" : "L" + std::to_string(i);
    auto level_query_result
      = query_->Collect(level_nodes_result, NodeNameEquals(level_name));
    EXPECT_TRUE(level_query_result);
  }

  // Assert: Should traverse deep hierarchy correctly
  EXPECT_TRUE(find_query_result);
  ASSERT_TRUE(node_result.has_value());
  EXPECT_EQ(node_result->GetName(), "L5_Target");

  // Should find all 6 nodes (L0 through L5_Target)
  std::optional<size_t> count_result;
  auto count_query_result
    = query_->Count(count_result, [](const ConstVisitedNode&) { return true; });
  EXPECT_TRUE(count_query_result);
  ASSERT_TRUE(count_result.has_value());
  EXPECT_EQ(count_result.value(), 6U);
}

NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_WithWideHierarchy_TraversesCorrectly)
{
  // Arrange: Create wide hierarchy using TestSceneFactory
  CreateForestScene(20, 15); // 20 roots with 15 children each = 320 nodes
  CreateQuery();

  // Act: Query the wide hierarchy
  std::optional<size_t> root_count_result;
  auto root_query_result
    = query_->Count(root_count_result, NodeNameStartsWith("Root"));

  std::optional<size_t> child_count_result;
  auto child_query_result
    = query_->Count(child_count_result, NodeNameStartsWith("Child"));

  std::optional<size_t> total_count_result;
  auto total_query_result = query_->Count(
    total_count_result, [](const ConstVisitedNode&) { return true; });

  std::vector<SceneNode> roots_result;
  auto collect_query_result
    = query_->Collect(roots_result, NodeNameStartsWith("Root"));

  // Assert: Should handle wide hierarchy correctly
  EXPECT_TRUE(root_query_result);
  EXPECT_TRUE(child_query_result);
  EXPECT_TRUE(total_query_result);
  EXPECT_TRUE(collect_query_result);

  ASSERT_TRUE(root_count_result.has_value());
  ASSERT_TRUE(child_count_result.has_value());
  ASSERT_TRUE(total_count_result.has_value());

  EXPECT_EQ(root_count_result.value(), 20U);
  EXPECT_GE(child_count_result.value(), 300U); // At least 20 * 15
  EXPECT_EQ(total_count_result.value(),
    root_count_result.value() + child_count_result.value());
  EXPECT_EQ(roots_result.size(), 20U);
}

NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_WithComplexFlags_FiltersCorrectly)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy();

  // Act: Query based on visibility flags
  std::optional<size_t> visible_enemies_result;
  auto visible_enemies_query_result = query_->Count(
    visible_enemies_result, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().starts_with("Enemy")
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    });

  std::optional<size_t> invisible_enemies_result;
  auto invisible_enemies_query_result = query_->Count(
    invisible_enemies_result, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().starts_with("Enemy")
        && !visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    });

  std::optional<size_t> static_nodes_result;
  auto static_nodes_query_result
    = query_->Count(static_nodes_result, [](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kStatic);
      });

  // Assert: Flag-based filtering should work correctly
  EXPECT_TRUE(visible_enemies_query_result);
  EXPECT_TRUE(invisible_enemies_query_result);
  EXPECT_TRUE(static_nodes_query_result);

  ASSERT_TRUE(visible_enemies_result.has_value());
  ASSERT_TRUE(invisible_enemies_result.has_value());
  ASSERT_TRUE(static_nodes_result.has_value());

  EXPECT_EQ(
    visible_enemies_result.value(), 2U); // Enemy1 and Enemy3 are visible
  EXPECT_EQ(invisible_enemies_result.value(), 1U); // Enemy2 is invisible
  EXPECT_GT(static_nodes_result.value(), 0U); // UI is static
}

//=== Real-World Scenario Tests ============================================//

//! Verifies that the batch query finds the player, equipment, and enemies in
//! a typical game object search scenario.
/*!
 Ensures all output variables are declared and checked outside the batch
 lambda. Covers normal and cross-object query scenarios for player, equipment,
 and enemy nodes in a typical gameplay hierarchy.
 @see SceneQuery::ExecuteBatch
*/
NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_GameObjectSearch_FindsPlayerAndEnemies)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy();

  // Output variables must outlive the batch lambda!
  std::optional<SceneNode> player_result;
  std::vector<SceneNode> equipment_result;
  std::optional<size_t> enemy_count;
  std::optional<size_t> visible_enemy_count;

  // Act: Simulate typical game queries
  auto player_search_result = query_->ExecuteBatch([&](auto& q) {
    // Find player
    q.BatchFindFirst(player_result, NodeNameEquals("Player"));
    // Get player equipment
    q.BatchCollect(equipment_result, [](const ConstVisitedNode& visited) {
      if (!visited.node_impl)
        return false;
      auto parent = visited.node_impl->AsGraphNode().GetParent();
      // This is a simplified check - in real code you'd walk up the
      // hierarchy
      return visited.node_impl->GetName() == "Weapon"
        || visited.node_impl->GetName() == "Shield";
    });
    // Count all enemies
    q.BatchCount(enemy_count, NodeNameStartsWith("Enemy"));
    // Check for active enemies (visible ones)
    q.BatchCount(visible_enemy_count, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().starts_with("Enemy")
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    });
  });

  // Assert: Game object search should complete successfully
  EXPECT_TRUE(player_search_result);
  EXPECT_TRUE(player_result.has_value());
  EXPECT_GE(equipment_result.size(), 1U);
  EXPECT_TRUE(enemy_count.has_value());
  EXPECT_TRUE(visible_enemy_count.has_value());
  EXPECT_GT(
    player_search_result.total_matches, 5U); // Player + equipment + enemies
}

//! Validates that asset-like hierarchies can be queried by resource type
//! (textures, models, audio) using batch queries.
/*!
 Ensures correct output variable usage and result validation outside the batch
 lambda. Covers normal and edge cases for asset resource queries in a nested
 hierarchy.
 @see SceneQuery::ExecuteBatch
*/
NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_AssetHierarchy_FindsResourcesByType)
{
  // Arrange: Create asset-like hierarchy using JSON
  const auto asset_json = R"({
      "metadata": {
        "name": "AssetHierarchy"
      },
      "nodes": [
        {
          "name": "Assets",
          "children": [
            {
              "name": "Textures",
              "children": [
                {"name": "diffuse_texture.png"},
                {"name": "normal_texture.png"},
                {"name": "specular_texture.png"}
              ]
            },
            {
              "name": "Models",
              "children": [
                {"name": "character_model.fbx"},
                {"name": "weapon_model.fbx"},
                {"name": "environment_model.fbx"}
              ]
            },
            {
              "name": "Sounds",
              "children": [
                {"name": "footstep_sound.wav"},
                {"name": "gunshot_sound.wav"}
              ]
            }
          ]
        }
      ]
    })";

  scene_ = GetFactory().CreateFromJson(asset_json, "AssetHierarchy");
  CreateQuery();

  // Output variables must outlive the batch lambda!
  std::vector<SceneNode> textures;
  std::vector<SceneNode> models;
  std::optional<size_t> audio_count;

  // Act: Query assets by type
  auto texture_search = query_->ExecuteBatch([&](auto& q) {
    // Find all textures
    q.BatchCollect(textures, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().ends_with(".png");
    });

    // Find all models
    q.BatchCollect(models, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().ends_with(".fbx");
    });

    // Find all audio files
    q.BatchCount(audio_count, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetName().ends_with(".wav");
    });
  });

  // Assert: Asset queries should work correctly
  EXPECT_EQ(textures.size(), 3);
  EXPECT_EQ(models.size(), 3);
  EXPECT_EQ(audio_count.value_or(0), 2);
  EXPECT_TRUE(texture_search);
  EXPECT_EQ(
    texture_search.total_matches, 8); // 3 textures + 3 models + 2 sounds
}

//! Tests that the batch query identifies renderable, UI, culled, and
//! shadow-casting nodes for scene optimization.
/*!
 All output variables and assertions are outside the batch lambda. Covers
 normal, edge, and cross-object scenarios for renderable and non-renderable
 node identification in a scene.
 @see SceneQuery::ExecuteBatch
*/
NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_SceneOptimization_IdentifiesRenderables)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy();

  // Output variables must outlive the batch lambda!
  std::vector<SceneNode> renderables;
  std::vector<SceneNode> ui_elements;
  std::optional<size_t> culled_count;
  std::optional<bool> shadow_casters;

  // Act: Identify renderable objects
  auto rendering_batch = query_->ExecuteBatch([&](const auto& q) {
    // Find all visible objects that need rendering
    q.BatchCollect(renderables, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible)
        && !visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kStatic);
    });

    // Find static UI elements (rendered differently)
    q.BatchCollect(ui_elements, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kStatic);
    });

    // Count invisible objects (culled from rendering)
    q.BatchCount(culled_count, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && !visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    });

    // Check if scene has any shadow casters
    q.BatchAny(shadow_casters, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kCastsShadows);
    });
  });

  // Assert: Rendering optimization queries should complete
  EXPECT_TRUE(rendering_batch);
  EXPECT_FALSE(renderables.empty());
  EXPECT_FALSE(ui_elements.empty());
  EXPECT_GT(culled_count.value_or(0), 0U); // Should have some invisible objects
  EXPECT_TRUE(shadow_casters.has_value());
  EXPECT_GT(rendering_batch.total_matches, 0);
}

//=== Cross-System Integration Tests =========================================//

NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_WithPathAndPredicates_CombinedApproach)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy(); // Act: Combine path and predicate approaches

  // First, use path to narrow down search space
  std::optional<SceneNode> weapon_node_result;
  auto weapon_query_result
    = query_->FindFirstByPath(weapon_node_result, "Level1/Player/Weapon");
  EXPECT_TRUE(weapon_query_result);
  ASSERT_TRUE(weapon_node_result
      .has_value()); // Then use predicates for complex filtering
  std::vector<SceneNode> level1_items_result;
  auto collect_query_result
    = query_->Collect(level1_items_result, [](const ConstVisitedNode& visited) {
        if (!visited.node_impl)
          return false;

        // Complex predicate: items that are not weapons and are visible
        return visited.node_impl->GetName() != "Weapon"
          && visited.node_impl->GetName() != "Shield"
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kVisible);
      });

  // Use batch for efficiency with mixed approaches
  std::optional<SceneNode> ui_root_result;
  std::vector<SceneNode> consumables_result;
  auto batch_query_result = query_->ExecuteBatch([&](const auto& q) {
    q.BatchFindFirst(ui_root_result, NodeNameEquals("UI"));
    q.BatchCollect(consumables_result, NodeNameStartsWith("Potion"));
  });

  // Assert: Combined approaches should work seamlessly
  EXPECT_TRUE(collect_query_result);
  EXPECT_TRUE(batch_query_result);
  EXPECT_EQ(weapon_node_result->GetName(), "Weapon");
  ASSERT_TRUE(ui_root_result.has_value());
  EXPECT_EQ(ui_root_result->GetName(), "UI");
  EXPECT_EQ(consumables_result.size(), 2U); // Potion1 and Potion2
}

NOLINT_TEST_F(
  SceneQueryIntegrationTest, Query_HierarchicalSearch_ParentChildRelationships)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy();

  // Output variables must outlive the batch lambda!
  std::vector<SceneNode> roots;
  std::vector<SceneNode> leaves;
  std::optional<size_t> intermediate_count;

  // Act: Find nodes based on parent-child relationships
  auto hierarchy_analysis = query_->ExecuteBatch([&](const auto& q) {
    // Find all root nodes (nodes without parents)
    q.BatchCollect(roots, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->AsGraphNode().GetParent().IsValid() == false;
    });

    // Find all leaf nodes (nodes without children)
    q.BatchCollect(leaves, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->AsGraphNode().GetFirstChild().IsValid() == false;
    });

    // Find intermediate nodes (have both parent and children)
    q.BatchCount(intermediate_count, [](const ConstVisitedNode& visited) {
      if (!visited.node_impl)
        return false;
      auto& graph = visited.node_impl->AsGraphNode();
      return graph.GetParent().IsValid() && graph.GetFirstChild().IsValid();
    });
  });

  // Assert: Hierarchical analysis should complete successfully
  EXPECT_TRUE(hierarchy_analysis);
  EXPECT_GE(roots.size(), 2); // Should have Level1 and UI as roots
  EXPECT_GT(leaves.size(), 5); // Many leaf nodes in the hierarchy
  EXPECT_GT(intermediate_count.value_or(0), 0U);
  EXPECT_GT(hierarchy_analysis.total_matches, 10); // Should find many nodes
}

NOLINT_TEST_F(SceneQueryIntegrationTest,
  Query_ComplexGameplayScenario_MultipleSystemsIntegration)
{
  // Arrange: Use default game scene
  CreateGameSceneHierarchy();

  // Act: Simulate game systems working together
  struct GameSystemQueries {
    std::optional<SceneNode> player;
    std::vector<SceneNode> visible_enemies;
    std::vector<SceneNode> nearby_items;
    std::vector<SceneNode> ui_elements;
    QueryResult performance_metrics;
    bool has_interactive_objects;
  };

  GameSystemQueries game_data;
  std::optional<size_t> performance_count;
  std::optional<bool> interactive_check;

  auto gameplay_batch = query_->ExecuteBatch([&](const auto& q) {
    // Player system: Find player
    q.BatchFindFirst(game_data.player, NodeNameEquals("Player"));

    // AI system: Find visible enemies for pathfinding
    q.BatchCollect(
      game_data.visible_enemies, [](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetName().starts_with("Enemy")
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kVisible);
      });

    // Item system: Find collectible items
    q.BatchCollect(game_data.nearby_items, NodeNameStartsWith("Potion"));

    // UI system: Find UI elements
    q.BatchCollect(game_data.ui_elements, [](const ConstVisitedNode& visited) {
      if (!visited.node_impl)
        return false;
      auto parent = visited.node_impl->AsGraphNode().GetParent();
      // Simplified check for UI children
      return visited.node_impl->GetName() == "MainMenu"
        || visited.node_impl->GetName() == "HealthBar"
        || visited.node_impl->GetName() == "Inventory";
    });

    // Performance system: Count total active objects
    q.BatchCount(performance_count, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    });

    // Interaction system: Check for interactive objects
    q.BatchAny(interactive_check, [](const ConstVisitedNode& visited) {
      return visited.node_impl
        && (visited.node_impl->GetName().starts_with("Potion")
          || visited.node_impl->GetName() == "Key");
    });
  });

  if (performance_count) {
    game_data.performance_metrics.nodes_matched = *performance_count;
  } else {
    game_data.performance_metrics.nodes_matched = 0;
  }

  game_data.has_interactive_objects = interactive_check.value_or(false);

  // Assert: Complex gameplay scenario should handle all systems
  EXPECT_TRUE(gameplay_batch);

  // Verify all systems got their data
  ASSERT_TRUE(game_data.player.has_value());
  EXPECT_EQ(game_data.player->GetName(), "Player");

  EXPECT_EQ(game_data.visible_enemies.size(), 2); // Enemy1 and Enemy3
  EXPECT_EQ(game_data.nearby_items.size(), 2); // Potion1 and Potion2
  EXPECT_GE(game_data.ui_elements.size(), 1); // At least some UI elements

  EXPECT_GT(game_data.performance_metrics.nodes_matched, 5);

  EXPECT_TRUE(game_data.has_interactive_objects);

  // Total batch should have found all these items
  std::size_t expected_total = 1 + // player
    game_data.visible_enemies.size() + game_data.nearby_items.size()
    + game_data.ui_elements.size() + game_data.performance_metrics.nodes_matched
    + (game_data.has_interactive_objects ? 1 : 0);

  EXPECT_EQ(gameplay_batch.total_matches, expected_total);
}

} // namespace
