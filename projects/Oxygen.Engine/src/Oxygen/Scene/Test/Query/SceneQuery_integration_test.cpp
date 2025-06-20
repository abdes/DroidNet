//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <vector>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  //=== Integration Test Fixture =============================================//

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

  //=== Complex Hierarchy Tests ==============================================//

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
    auto count_query_result = query_->Count(
      count_result, [](const ConstVisitedNode&) { return true; });
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

  NOLINT_TEST_F(
    SceneQueryIntegrationTest, Query_GameObjectSearch_FindsPlayerAndEnemies)
  { // Arrange: Use default game scene
    CreateGameSceneHierarchy();

    // Act: Simulate typical game queries
    auto player_search_result = query_->ExecuteBatch([&](const auto& q) {
      // Find player
      std::optional<SceneNode> player_result;
      q.BatchFindFirst(player_result, NodeNameEquals("Player"));
      EXPECT_TRUE(player_result.has_value());

      // Get player equipment
      std::vector<SceneNode> equipment_result;
      auto equipment_query_result
        = q.Collect(equipment_result, [](const ConstVisitedNode& visited) {
            if (!visited.node_impl)
              return false;
            auto parent = visited.node_impl->AsGraphNode().GetParent();
            // This is a simplified check - in real code you'd walk up the
            // hierarchy
            return visited.node_impl->GetName() == "Weapon"
              || visited.node_impl->GetName() == "Shield";
          });
      EXPECT_TRUE(equipment_query_result);
      EXPECT_GE(equipment_result.size(), 1U);

      // Count all enemies
      q.BatchCount(NodeNameStartsWith("Enemy"));

      // Check for active enemies (visible ones)
      q.BatchCount([](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetName().starts_with("Enemy")
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kVisible);
      });
    });

    // Assert: Game object search should complete successfully
    EXPECT_TRUE(player_search_result);
    EXPECT_GT(
      player_search_result.total_matches, 5U); // Player + equipment + enemies
  }

  NOLINT_TEST_F(
    SceneQueryIntegrationTest, Query_AssetHierarchy_FindsResourcesByType)
  {
    // Arrange: Create asset-like hierarchy using JSON
    const auto asset_json = R"({
      "scene": {
        "name": "AssetHierarchy",
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
      }
    })";

    scene_ = GetFactory().CreateFromJson(asset_json, "AssetHierarchy");
    CreateQuery();

    // Act: Query assets by type
    auto texture_search = query_->ExecuteBatch([&](const auto& q) {
      // Find all textures
      std::vector<SceneNode> textures;
      auto texture_result
        = q.Collect(textures, [](const ConstVisitedNode& visited) {
            return visited.node_impl
              && visited.node_impl->GetName().ends_with(".png");
          });
      EXPECT_TRUE(texture_result);
      EXPECT_EQ(textures.size(), 3);

      // Find all models
      std::vector<SceneNode> models;
      auto model_result
        = q.Collect(models, [](const ConstVisitedNode& visited) {
            return visited.node_impl
              && visited.node_impl->GetName().ends_with(".fbx");
          });
      EXPECT_TRUE(model_result);
      EXPECT_EQ(models.size(), 3);

      // Find all audio files
      auto audio_count = q.Count([](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetName().ends_with(".wav");
      });
      EXPECT_EQ(audio_count.nodes_matched, 2);
    });

    // Assert: Asset queries should work correctly
    EXPECT_TRUE(texture_search);
    EXPECT_EQ(
      texture_search.total_matches, 8); // 3 textures + 3 models + 2 sounds
  }

  NOLINT_TEST_F(
    SceneQueryIntegrationTest, Query_SceneOptimization_IdentifiesRenderables)
  {
    // Arrange: Use default game scene
    CreateGameSceneHierarchy();

    // Act: Identify renderable objects
    auto rendering_batch = query_->ExecuteBatch([&](const auto& q) {
      // Find all visible objects that need rendering
      std::vector<SceneNode> renderables;
      auto renderable_result
        = q.Collect(renderables, [](const ConstVisitedNode& visited) {
            return visited.node_impl
              && visited.node_impl->GetFlags().GetEffectiveValue(
                SceneNodeFlags::kVisible)
              && !visited.node_impl->GetFlags().GetEffectiveValue(
                SceneNodeFlags::kStatic);
          });
      EXPECT_TRUE(renderable_result);

      // Find static UI elements (rendered differently)
      std::vector<SceneNode> ui_elements;
      auto ui_result
        = q.Collect(ui_elements, [](const ConstVisitedNode& visited) {
            return visited.node_impl
              && visited.node_impl->GetFlags().GetEffectiveValue(
                SceneNodeFlags::kStatic);
          });
      EXPECT_TRUE(ui_result);

      // Count invisible objects (culled from rendering)
      auto culled_count = q.Count([](const ConstVisitedNode& visited) {
        return visited.node_impl
          && !visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kVisible);
      });
      EXPECT_GT(
        culled_count.nodes_matched, 0); // Should have some invisible objects

      // Check if scene has any shadow casters
      auto shadow_casters = q.Any([](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kCastsShadows);
      });
      EXPECT_TRUE(shadow_casters.has_value());
    });

    // Assert: Rendering optimization queries should complete
    EXPECT_TRUE(rendering_batch);
    EXPECT_GT(rendering_batch.total_matches, 0);
  }

  //=== Cross-System Integration Tests =======================================//

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
    auto collect_query_result = query_->Collect(
      level1_items_result, [](const ConstVisitedNode& visited) {
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
      q.Collect(consumables_result, NodeNameStartsWith("Potion"));
    });

    // Assert: Combined approaches should work seamlessly
    EXPECT_TRUE(collect_query_result);
    EXPECT_TRUE(batch_query_result);
    EXPECT_EQ(weapon_node_result->GetName(), "Weapon");
    ASSERT_TRUE(ui_root_result.has_value());
    EXPECT_EQ(ui_root_result->GetName(), "UI");
    EXPECT_EQ(consumables_result.size(), 2U); // Potion1 and Potion2
  }

  NOLINT_TEST_F(SceneQueryIntegrationTest,
    Query_HierarchicalSearch_ParentChildRelationships)
  {
    // Arrange: Use default game scene
    CreateGameSceneHierarchy();

    // Act: Find nodes based on parent-child relationships
    auto hierarchy_analysis = query_->ExecuteBatch([&](const auto& q) {
      // Find all root nodes (nodes without parents)
      std::vector<SceneNode> roots;
      auto root_result = q.Collect(roots, [](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->AsGraphNode().GetParent().IsValid() == false;
      });
      EXPECT_TRUE(root_result);
      EXPECT_GE(roots.size(), 2); // Should have Level1 and UI as roots

      // Find all leaf nodes (nodes without children)
      std::vector<SceneNode> leaves;
      auto leaf_result = q.Collect(leaves, [](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->AsGraphNode().GetFirstChild().IsValid()
          == false;
      });
      EXPECT_TRUE(leaf_result);
      EXPECT_GT(leaves.size(), 5); // Many leaf nodes in the hierarchy

      // Find intermediate nodes (have both parent and children)
      auto intermediate_count = q.Count([](const ConstVisitedNode& visited) {
        if (!visited.node_impl)
          return false;
        auto& graph = visited.node_impl->AsGraphNode();
        return graph.GetParent().IsValid() && graph.GetFirstChild().IsValid();
      });
      EXPECT_GT(intermediate_count.nodes_matched, 0);
    });

    // Assert: Hierarchical analysis should complete successfully
    EXPECT_TRUE(hierarchy_analysis);
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
    auto gameplay_batch = query_->ExecuteBatch([&](const auto& q) {
      // Player system: Find player
      q.BatchFindFirst(game_data.player, NodeNameEquals("Player"));

      // AI system: Find visible enemies for pathfinding
      q.Collect(game_data.visible_enemies, [](const ConstVisitedNode& visited) {
        return visited.node_impl
          && visited.node_impl->GetName().starts_with("Enemy")
          && visited.node_impl->GetFlags().GetEffectiveValue(
            SceneNodeFlags::kVisible);
      });

      // Item system: Find collectible items
      q.Collect(game_data.nearby_items, NodeNameStartsWith("Potion"));

      // UI system: Find UI elements
      q.Collect(game_data.ui_elements, [](const ConstVisitedNode& visited) {
        if (!visited.node_impl)
          return false;
        auto parent = visited.node_impl->AsGraphNode().GetParent();
        // Simplified check for UI children
        return visited.node_impl->GetName() == "MainMenu"
          || visited.node_impl->GetName() == "HealthBar"
          || visited.node_impl->GetName() == "Inventory";
      });

      // Performance system: Count total active objects
      game_data.performance_metrics
        = q.Count([](const ConstVisitedNode& visited) {
            return visited.node_impl
              && visited.node_impl->GetFlags().GetEffectiveValue(
                SceneNodeFlags::kVisible);
          });

      // Interaction system: Check for interactive objects
      auto interactive_check = q.Any([](const ConstVisitedNode& visited) {
        return visited.node_impl
          && (visited.node_impl->GetName().starts_with("Potion")
            || visited.node_impl->GetName() == "Key");
      });
      game_data.has_interactive_objects = interactive_check.value_or(false);
    });

    // Assert: Complex gameplay scenario should handle all systems
    EXPECT_TRUE(gameplay_batch);

    // Verify all systems got their data
    ASSERT_TRUE(game_data.player.has_value());
    EXPECT_EQ(game_data.player->GetName(), "Player");

    EXPECT_EQ(game_data.visible_enemies.size(), 2); // Enemy1 and Enemy3
    EXPECT_EQ(game_data.nearby_items.size(), 2); // Potion1 and Potion2
    EXPECT_GE(game_data.ui_elements.size(), 1); // At least some UI elements

    EXPECT_TRUE(game_data.performance_metrics);
    EXPECT_GT(game_data.performance_metrics.nodes_matched, 5);

    EXPECT_TRUE(game_data.has_interactive_objects);

    // Total batch should have found all these items
    std::size_t expected_total = 1 + // player
      game_data.visible_enemies.size() + game_data.nearby_items.size()
      + game_data.ui_elements.size()
      + game_data.performance_metrics.nodes_matched
      + (game_data.has_interactive_objects ? 1 : 0);

    EXPECT_EQ(gameplay_batch.total_matches, expected_total);
  }

} // namespace

} // namespace oxygen::scene::testing
