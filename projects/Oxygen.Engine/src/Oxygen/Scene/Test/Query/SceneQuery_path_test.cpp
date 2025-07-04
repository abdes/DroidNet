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

//=== Path-Based Query Test Fixture ===---------------------------------------//

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

//=== FindFirstByPath Tests ===-----------------------------------------------//

//! Find first node by intermediate path
NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_PathToIntermediateNode_FindsNode)
{
  // Arrange: Use game scene hierarchy with known paths
  // Hierarchy: GameWorld -> Players -> [Player1, Player2]
  CreateMultiPlayerHierarchy();
  // Act: Find specific player using absolute path
  std::optional<SceneNode> result;
  auto query_result = query_->FindFirstByPath(result, "GameWorld/Player1");
  // Assert: Should find Player1 node
  EXPECT_TRUE(query_result);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Player1");
}

//! Find node by deep absolute path
NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithDeepPath_FindsCorrectNode)
{
  // Arrange: Game scene with deep equipment paths
  CreateMultiPlayerHierarchy();
  // Act: Find equipment using deep absolute path
  std::optional<SceneNode> result;
  auto query_result
    = query_->FindFirstByPath(result, "GameWorld/Player1/Weapon");
  // Assert: Should find the weapon node
  EXPECT_TRUE(query_result);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Weapon");
}

//! Find node by invalid path returns nullopt
NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithInvalidPath_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  // Act: Try to find non-existent path
  std::optional<SceneNode> result;
  auto query_result
    = query_->FindFirstByPath(result, "GameWorld/NonExistent/Path");
  // Assert: Should return nullopt
  EXPECT_TRUE(query_result); // Query should succeed but find nothing
  EXPECT_FALSE(result.has_value());
}

//! Find node by empty path returns nullopt
NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithEmptyPath_ReturnsNullopt)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  // Act: Try to find with empty path
  std::optional<SceneNode> result;
  auto query_result = query_->FindFirstByPath(result, "");
  // Assert: Should return nullopt
  EXPECT_TRUE(query_result); // Query should succeed but find nothing
  EXPECT_FALSE(result.has_value());
}

//! Find root node by path
NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithRootPath_FindsRoot)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  // Act: Find root node
  std::optional<SceneNode> result;
  auto query_result = query_->FindFirstByPath(result, "Level1");
  // Assert: Should find Level1 root
  EXPECT_TRUE(query_result);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Level1");
  // Act: Find the other root node
  query_result = query_->FindFirstByPath(result, "UI");
  // Assert: Should find UI root
  EXPECT_TRUE(query_result);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "UI");
}

//! Find node by path with scoped traversal
NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithScopedTraversal_FindsWithinScope)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  // Find Player1 subtree to scope
  std::optional<SceneNode> player1_node;
  [[maybe_unused]] auto _
    = query_->FindFirst(player1_node, NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());
  // Act: Find weapon within Player1 scope using relative path
  query_->AddToTraversalScope(*player1_node);
  std::optional<SceneNode> scoped_weapon;
  auto scoped_query_result
    = query_->FindFirstByPath(scoped_weapon, "Player1/Weapon");
  // Reset and try same path in full scope
  query_->ResetTraversalScope();
  std::optional<SceneNode> full_scope_weapon;
  auto full_scope_query_result
    = query_->FindFirstByPath(full_scope_weapon, "Level1/Player1/Weapon");
  // Assert: Both should find weapons but scoped search is more efficient
  EXPECT_TRUE(scoped_query_result);
  EXPECT_TRUE(full_scope_query_result);
  ASSERT_TRUE(scoped_weapon.has_value());
  ASSERT_TRUE(full_scope_weapon.has_value());
  EXPECT_EQ(scoped_weapon->GetName(), "Weapon");
  EXPECT_EQ(full_scope_weapon->GetName(), "Weapon");
  // Should be the same weapon node
  EXPECT_EQ(scoped_weapon->GetHandle(), full_scope_weapon->GetHandle());
}

//! Handles malformed path patterns gracefully (should not crash, returns error)
NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithMalformedPaths_HandlesGracefully)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::optional<SceneNode> result;

  // Act: Try invalid control character (ASCII 0x01)
  std::string path_with_ctrl
    = std::string("Level1/") + std::string(1, '\x01') + "/Weapon";
  auto query_result1 = query_->FindFirstByPath(result, path_with_ctrl);

  // Act: Try unterminated escape sequence
  auto query_result2
    = query_->FindFirstByPath(result, "Level1/Player1/Weapon\\");

  // Act: Try invalid escape sequence
  auto query_result3 = query_->FindFirstByPath(result, "Level1/Player1/\\z");

  // Assert: Should not throw, should return error or nullopt
  EXPECT_FALSE(query_result1); // Should indicate error
  EXPECT_FALSE(query_result2);
  EXPECT_FALSE(query_result3);
}

//=== CollectByPath Tests ===-------------------------------------------------//

//! Collect all direct children of Level1 using simple pattern
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
  EXPECT_TRUE(result);

  // Extract node names for comparison
  std::vector<std::string> child_names;
  std::ranges::transform(level1_children, std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers
  EXPECT_THAT(child_names,
    AllOf(SizeIs(4),
      UnorderedElementsAre("Player1", "Player2", "Enemies", "Items")));
}

//! Collect all direct children of Player1 using single wildcard
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
  EXPECT_TRUE(result);
  EXPECT_THAT(player_equipment, SizeIs(3));
  // Extract node names for verification
  std::vector<std::string> equipment_names;
  std::ranges::transform(player_equipment, std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain expected equipment types
  EXPECT_THAT(equipment_names,
    AllOf(SizeIs(3), UnorderedElementsAre("Weapon", "Shield", "Armor")));
}

//! Collect all Weapon nodes recursively using double wildcard
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

  // Assert: Should find the one weapon under each Player
  EXPECT_TRUE(result);
  EXPECT_THAT(weapons, SizeIs(2));
  EXPECT_THAT(weapons, Each(Property(&SceneNode::GetName, "Weapon")));
}

//! Collect all enemies using complex pattern
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
  EXPECT_TRUE(result);
  EXPECT_THAT(enemies, SizeIs(3));
  // Extract names for verification
  std::vector<std::string> enemy_names;
  std::ranges::transform(enemies, std::back_inserter(enemy_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain all enemy nodes
  EXPECT_THAT(enemy_names,
    AllOf(
      SizeIs(3), Contains("Enemy1"), Contains("Enemy2"), Contains("Enemy3")));
}

//! Collect by pattern with no matches returns empty container
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithNoMatches_ReturnsEmptyContainer)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Try to collect non-existent pattern
  const auto result = query_->CollectByPath(nodes, "**/NonExistent");

  // Assert: Should return empty container
  EXPECT_TRUE(result);
  EXPECT_TRUE(nodes.empty());
}

//! Collect by exact path to single node
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
  EXPECT_TRUE(result);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Weapon");
}

//! Collect all equipment within Player1 scope using path
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithScopedTraversal_CollectsWithinScope)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::Not;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::optional<SceneNode> player1_node;
  [[maybe_unused]] auto _
    = query_->FindFirst(player1_node, NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());
  // Act: Collect all equipment within Player1 scope
  std::vector<SceneNode> scoped_equipment;
  query_->AddToTraversalScope(*player1_node);
  const auto result = query_->CollectByPath(scoped_equipment, "Player1/*");
  // Assert: Should collect only Player1's equipment
  EXPECT_TRUE(result);
  EXPECT_THAT(scoped_equipment, SizeIs(3));
  // Extract names for verification
  std::vector<std::string> equipment_names;
  std::ranges::transform(scoped_equipment, std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain Player1's equipment but not other players'
  EXPECT_THAT(equipment_names,
    AllOf(
      SizeIs(3), Contains("Weapon"), Contains("Shield"), Contains("Armor")));
}

//! Collect weapons from multiple scoped players
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithMultipleScopedNodes_CollectsFromAll)
{
  using testing::AllOf;
  using testing::Contains;
  using testing::Not;
  using testing::SizeIs;

  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();

  std::optional<SceneNode> player1_node;
  [[maybe_unused]] auto _1
    = query_->FindFirst(player1_node, NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  std::optional<SceneNode> player2_node;
  [[maybe_unused]] auto _2
    = query_->FindFirst(player2_node, NodeNameEquals("Player2"));
  ASSERT_TRUE(player2_node.has_value());

  // Act: Add multiple nodes to traversal scope
  std::vector<SceneNode> scope_nodes = { *player1_node, *player2_node };
  query_->AddToTraversalScope(scope_nodes);

  std::vector<SceneNode> collected_weapons;
  const auto result = query_->CollectByPath(collected_weapons, "*/Weapon");

  // Assert: Should collect weapons from both scoped players
  EXPECT_TRUE(result);
  EXPECT_THAT(collected_weapons, SizeIs(2));
  // Extract names to verify all are weapons
  std::vector<std::string> weapon_names;
  std::ranges::transform(collected_weapons, std::back_inserter(weapon_names),
    [](const SceneNode& node) { return node.GetName(); });
  // All should be weapons, none from outside scope
  EXPECT_THAT(weapon_names,
    AllOf(SizeIs(2), Contains("Weapon"), Not(Contains("Merchant"))));
}

//! Collect all Weapon nodes at any depth using nested wildcards
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
  EXPECT_TRUE(result);
  EXPECT_THAT(all_weapons, SizeIs(2));
  EXPECT_THAT(all_weapons, Each(Property(&SceneNode::GetName, "Weapon")));
}

//! Collect direct children of any root using wildcard
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
  EXPECT_TRUE(result);
  std::vector<std::string> child_names;
  std::ranges::transform(root_children, std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });
  EXPECT_THAT(child_names,
    AllOf(SizeIs(7),
      IsSupersetOf({ "Player1", "Player2", "Enemies", "Items", "MainMenu",
        "HealthBar", "Inventory" })));
}

//! Collect all nodes in large hierarchy using recursive wildcard
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
  EXPECT_TRUE(result);
  EXPECT_THAT(all_nodes, SizeIs(200));
}

//! Collect all nodes at any depth using global wildcard
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
  EXPECT_TRUE(result);
  std::vector<std::string> node_names;
  std::ranges::transform(all_nodes, std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });
  EXPECT_THAT(node_names,
    AllOf(SizeIs(21), Contains("Level1"), Contains("UI"), Contains("Player1"),
      Contains("Player2"), Contains("Enemies"), Contains("Items"),
      Contains("Weapon"), Contains("Shield"), Contains("Armor"),
      Contains("Weapon"), Contains("Bow"), Contains("Quiver"),
      Contains("Enemy1"), Contains("Enemy2"), Contains("Enemy3"),
      Contains("Potion1"), Contains("Potion2"), Contains("Key"),
      Contains("MainMenu"), Contains("HealthBar"), Contains("Inventory")));
}

//=== Error Conditions ===----------------------------------------------------//

//! Path queries on empty scene handle gracefully
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithEmptyScene_HandlesGracefully)
{
  // Arrange: Create empty scene
  CreateEmptyScene();
  // Act: Perform path queries on empty scene
  std::optional<SceneNode> find_result;
  auto find_query_result = query_->FindFirstByPath(find_result, "Any/Path");
  std::vector<SceneNode> nodes;
  const auto collect_result = query_->CollectByPath(nodes, "**/*");
  // Assert: All operations should complete gracefully
  EXPECT_TRUE(find_query_result); // Query should succeed but find nothing
  EXPECT_FALSE(find_result.has_value());
  EXPECT_TRUE(collect_result);
  EXPECT_TRUE(nodes.empty());
}

//! Path queries on single-node scene work correctly
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSingleNodeScene_WorksCorrectly)
{
  using testing::SizeIs;
  // Arrange: Create simple single node scene
  CreateSimpleScene();
  std::vector<SceneNode> nodes;
  // Act: Query the single node scene with path
  std::optional<SceneNode> find_result;
  auto find_query_result = query_->FindFirstByPath(find_result, "Root");
  const auto collect_result = query_->CollectByPath(nodes, "Root");
  // Assert: Should handle single node correctly
  EXPECT_TRUE(find_query_result);
  ASSERT_TRUE(find_result.has_value());
  EXPECT_EQ(find_result->GetName(), "Root");
  EXPECT_TRUE(collect_result);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Root");
}

//! Handles malformed path patterns in collect (should not crash, returns error)
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithMalformedPaths_HandlesGracefully)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Try invalid control character (ASCII 0x01)
  std::string path_with_ctrl
    = std::string("Level1/") + std::string(1, '\x01') + "/Weapon";
  auto result1 = query_->CollectByPath(nodes, path_with_ctrl);

  // Act: Try unterminated escape sequence
  auto result2 = query_->CollectByPath(nodes, "Level1/*/Weapon\\");

  // Act: Try invalid escape sequence
  auto result3 = query_->CollectByPath(nodes, "**/\\z");

  // Assert: Should not throw, should return error
  EXPECT_FALSE(result1);
  EXPECT_FALSE(result2);
  EXPECT_FALSE(result3);
}

//! Handles very long path patterns gracefully (should not crash, returns empty)
NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithVeryLongPaths_HandlesGracefully)
{
  // Arrange: Use default game scene hierarchy
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Use a very long path
  std::string long_path(10000, 'A');
  auto result = query_->CollectByPath(nodes, long_path);

  // Assert: Should not throw, should return success (true) and empty result
  EXPECT_TRUE(result);
  EXPECT_TRUE(nodes.empty());
}

} // namespace
