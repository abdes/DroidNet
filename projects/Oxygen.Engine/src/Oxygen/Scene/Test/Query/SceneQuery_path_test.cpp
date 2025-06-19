//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "SceneQueryTestBase.h"

using oxygen::scene::SceneNode;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Path-Based Query Test Fixture ===-------------------------------------//

class SceneQueryPathTest : public SceneQueryTestBase {
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
                  {"name": "Weapon", "flags": {"visible": false}},
                  {"name": "Bow", "flags": {"visible": true}},
                  {"name": "Quiver", "flags": {"visible": false}}
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

//=== FindFirstByPath Tests ===---------------------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_PathToIntermediateNode_FindsNode)
{
  // Arrange: Use game scene hierarchy with known paths
  // Hierarchy: GameWorld -> Players -> [Player1, Player2]
  CreateMultiPlayerHierarchy();

  // Act: Find specific player using absolute path
  const auto result = query_->FindFirstByPath("GameWorld/Player1");

  // Assert: Should find Player1 node
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Player1");
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithDeepPath_FindsCorrectNode)
{
  // Arrange: Game scene with deep equipment paths
  CreateMultiPlayerHierarchy();

  // Act: Find equipment using deep absolute path
  const auto result = query_->FindFirstByPath("GameWorld/Player1/Weapon");

  // Assert: Should find the weapon node
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Weapon");
}

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithInvalidPath_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  // Act: Try to find non-existent path
  const auto result = query_->FindFirstByPath("GameWorld/NonExistent/Path");

  // Assert: Should return nullopt
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithEmptyPath_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  // Act: Try to find with empty path
  const auto result = query_->FindFirstByPath("");

  // Assert: Should return nullopt
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithRootPath_FindsRoot)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  // Act: Find root node
  auto result = query_->FindFirstByPath("Level1");

  // Assert: Should find GameWorld root
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Level1");

  // Act: Find the other root node
  result = query_->FindFirstByPath("UI");

  // Assert: Should find GameWorld root
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "UI");
}

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithScopedTraversal_FindsWithinScope)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  // Find Player1 subtree to scope
  const auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Find weapon within Player1 scope using relative path
  query_->AddToTraversalScope(*player1_node);
  const auto scoped_weapon = query_->FindFirstByPath("Player1/Weapon");

  // Reset and try same path in full scope
  query_->ResetTraversalScope();
  const auto full_scope_weapon
    = query_->FindFirstByPath("Level1/Player1/Weapon");

  // Assert: Both should find weapons but scoped search is more efficient
  ASSERT_TRUE(scoped_weapon.has_value());
  ASSERT_TRUE(full_scope_weapon.has_value());
  EXPECT_EQ(scoped_weapon->GetName(), "Weapon");
  EXPECT_EQ(full_scope_weapon->GetName(), "Weapon");
  // Should be the same weapon node
  EXPECT_EQ(scoped_weapon->GetHandle(), full_scope_weapon->GetHandle());
}

//=== CollectByPath Tests ===-----------------------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSimplePattern_CollectsMatches)
{
  using testing::AllOf;
  using testing::SizeIs;
  using testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> level1_children;

  // Act: Collect all direct children of Level1
  const auto result = query_->CollectByPath(level1_children, "Level1/*");

  // Assert: Should collect Player, Enemies, Items
  EXPECT_TRUE(result.completed);

  // Extract node names for comparison
  std::vector<std::string> child_names;
  std::ranges::transform(level1_children, std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers
  EXPECT_THAT(child_names,
    AllOf(SizeIs(4),
      UnorderedElementsAre("Player1", "Player2", "Enemies", "Items")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSingleWildcard_CollectsDirectChildren)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::SizeIs;
  using testing::UnorderedElementsAre;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> player_equipment;

  // Act: Collect all direct children of Player using wildcard
  const auto result
    = query_->CollectByPath(player_equipment, "Level1/Player1/*");

  // Assert: Should collect Player's equipment
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(player_equipment, SizeIs(3)); // Weapon, Shield, Armor
  // Extract node names for verification
  std::vector<std::string> equipment_names;
  std::ranges::transform(player_equipment, std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain expected equipment types
  EXPECT_THAT(equipment_names,
    AllOf(SizeIs(3), UnorderedElementsAre("Weapon", "Shield", "Armor")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithRecursiveWildcard_CollectsAllDepths)
{
  using testing::AllOf;
  using testing::Each;
  using testing::Property;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> weapons;

  // Act: Collect all Weapon nodes recursively using double wildcard
  const auto result = query_->CollectByPath(weapons, "**/Weapon");

  // Assert: Should find the one weapon under Player
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(weapons, SizeIs(2)); // One weapon undereach Player

  // All collected nodes should be named "Weapon"
  EXPECT_THAT(weapons, Each(Property(&SceneNode::GetName, "Weapon")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithComplexPattern_CollectsCorrectly)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> enemies;

  // Act: Collect all enemies using complex pattern
  const auto result = query_->CollectByPath(enemies, "Level1/Enemies/*");

  // Assert: Should collect all 3 enemy nodes
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(enemies, SizeIs(3)); // Enemy1, Enemy2, Enemy3
  // Extract names for verification
  std::vector<std::string> enemy_names;
  std::ranges::transform(enemies, std::back_inserter(enemy_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain all enemy nodes
  EXPECT_THAT(enemy_names,
    AllOf(
      SizeIs(3), Contains("Enemy1"), Contains("Enemy2"), Contains("Enemy3")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithNoMatches_ReturnsEmptyContainer)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Try to collect non-existent pattern
  const auto result = query_->CollectByPath(nodes, "**/NonExistent");

  // Assert: Should return empty container
  EXPECT_TRUE(result.completed);
  EXPECT_TRUE(nodes.empty());
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithExactPath_CollectsSingleNode)
{
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Collect using exact path to single node
  const auto result = query_->CollectByPath(nodes, "Level1/Player1/Weapon");

  // Assert: Should collect exactly one node
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Weapon");
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithScopedTraversal_CollectsWithinScope)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::Not;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  // Find Player1 subtree to scope
  const auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Collect all equipment within Player1 scope
  std::vector<SceneNode> scoped_equipment;
  query_->AddToTraversalScope(*player1_node);
  const auto result = query_->CollectByPath(scoped_equipment, "Player1/*");

  // Assert: Should collect only Player1's equipment
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(scoped_equipment, SizeIs(3)); // Weapon, Shield, Armor
  // Extract names for verification
  std::vector<std::string> equipment_names;
  std::ranges::transform(scoped_equipment, std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain Player1's equipment but not other players'
  EXPECT_THAT(equipment_names,
    AllOf(
      SizeIs(3), Contains("Weapon"), Contains("Shield"), Contains("Armor")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithMultipleScopedNodes_CollectsFromAll)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::Not;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  const auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  const auto player2_node = query_->FindFirst(NodeNameEquals("Player2"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(player2_node.has_value());

  // Act: Add multiple nodes to traversal scope
  std::vector<SceneNode> scope_nodes = { *player1_node, *player2_node };
  query_->AddToTraversalScope(scope_nodes);

  std::vector<SceneNode> collected_weapons;
  const auto result = query_->CollectByPath(collected_weapons, "*/Weapon");

  // Assert: Should collect weapons from both scoped players
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(collected_weapons, SizeIs(2)); // One weapon from each player

  // Extract names to verify all are weapons
  std::vector<std::string> weapon_names;
  std::ranges::transform(collected_weapons, std::back_inserter(weapon_names),
    [](const SceneNode& node) { return node.GetName(); });
  // All should be weapons, none from outside scope
  EXPECT_THAT(weapon_names,
    AllOf(SizeIs(2), Contains("Weapon"), Not(Contains("Merchant"))));
}

//=== Edge Cases and Complex Patterns ===----------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithNestedWildcards_WorksCorrectly)
{
  using testing::AllOf;
  using testing::Each;
  using testing::Property;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> all_weapons;

  // Act: Find all Weapon nodes at any depth using nested wildcards
  const auto result = query_->CollectByPath(all_weapons, "**/Weapon");

  // Assert: Should find weapons from both players
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(
    all_weapons, SizeIs(2)); // One weapon per player (Player1, Player2)

  // All collected nodes should be named "Weapon"
  EXPECT_THAT(all_weapons, Each(Property(&SceneNode::GetName, "Weapon")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithRootWildcard_CollectsFromAllRoots)
{
  using testing::AllOf;
  using testing::IsSupersetOf;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> root_children;

  // Act: Collect direct children of any root using wildcard
  const auto result = query_->CollectByPath(root_children, "*/*");

  // Assert: Should collect children from both Level1 and UI roots
  EXPECT_TRUE(result.completed);

  // Extract names for verification
  std::vector<std::string> child_names;
  std::ranges::transform(root_children, std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers - should contain children from both
  // roots
  EXPECT_THAT(child_names,
    AllOf(SizeIs(7), // 4 from Level1 + 3 from UI
      IsSupersetOf({ "Player1", "Player2", "Enemies", "Items", "MainMenu",
        "HealthBar", "Inventory" })));
}

NOLINT_TEST_F(SceneQueryPathTest, CollectByPath_Performance_WithLargeHierarchy)
{
  using testing::SizeIs;

  // Arrange: Create large hierarchy for performance test
  CreateForestScene(10, 20); // 10 roots with 20 children each
  CreateQuery();
  std::vector<SceneNode> all_nodes;

  // Act: Collect all nodes using recursive wildcard
  const auto result = query_->CollectByPath(all_nodes, "*/*");

  // Assert: Should complete successfully with many nodes
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(
    all_nodes, SizeIs(200)); // 10 roots × 20 children = 200 child nodes
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithGlobalWildcard_CollectsAllNodes)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> all_nodes;

  // Act: Collect all nodes at any depth using global wildcard
  const auto result = query_->CollectByPath(all_nodes, "**/*");

  // Assert: Should collect every node in the scene
  EXPECT_TRUE(result.completed);

  // Extract node names for verification
  std::vector<std::string> node_names;
  std::ranges::transform(
    all_nodes, std::back_inserter(node_names), [](const SceneNode& node) {
      return node.GetName();
    }); // Should contain all nodes from the GameScene hierarchy (18 total
        // nodes)
  EXPECT_THAT(node_names,
    AllOf(SizeIs(21), // Level1 + UI + 4 Level1 groups + 3 UI nodes + 11
                      // equipment/enemies/items = 18 total
      Contains("Level1"), // roots
      Contains("UI"),
      Contains("Player1"), // Level1 groups
      Contains("Player2"), Contains("Enemies"), Contains("Items"),
      Contains("Weapon"), // Player1 equipment
      Contains("Shield"), Contains("Armor"),
      Contains("Weapon"), // Player2 equipment
      Contains("Bow"), // Player2 equipment
      Contains("Quiver"),
      Contains("Enemy1"), // Enemies
      Contains("Enemy2"), Contains("Enemy3"),
      Contains("Potion1"), // Items
      Contains("Potion2"), Contains("Key"),
      Contains("MainMenu"), // UI elements
      Contains("HealthBar"), Contains("Inventory")));
}

//=== Error Conditions ===--------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithEmptyScene_HandlesGracefully)
{
  // Arrange: Create empty scene
  CreateEmptyScene();

  // Act: Perform path queries on empty scene
  const auto find_result = query_->FindFirstByPath("Any/Path");
  std::vector<SceneNode> nodes;
  const auto collect_result = query_->CollectByPath(nodes, "**/*");

  // Assert: All operations should complete gracefully
  EXPECT_FALSE(find_result.has_value());
  EXPECT_TRUE(collect_result.completed);
  EXPECT_TRUE(nodes.empty());
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSingleNodeScene_WorksCorrectly)
{
  using testing::SizeIs;

  // Arrange: Create simple single node scene
  CreateSimpleScene();
  std::vector<SceneNode> nodes;

  // Act: Query the single node scene with path
  const auto find_result = query_->FindFirstByPath("Root");
  const auto collect_result = query_->CollectByPath(nodes, "Root");

  // Assert: Should handle single node correctly
  ASSERT_TRUE(find_result.has_value());
  EXPECT_EQ(find_result->GetName(), "Root");
  EXPECT_TRUE(collect_result.completed);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Root");
}

} // namespace
