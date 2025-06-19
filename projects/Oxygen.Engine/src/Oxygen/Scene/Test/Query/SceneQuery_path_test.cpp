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

class SceneQueryPathTest : public SceneQueryTestBase { };

//=== FindFirstByPath Tests ===---------------------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_PathToIntermediateNode_FindsNode)
{
  // Arrange: Use game scene hierarchy with known paths
  // Hierarchy: GameWorld -> Players -> [Player1, Player2]
  CreateMultiPlayerHierarchy();

  // Act: Find specific player using absolute path
  auto result = query_->FindFirstByPath("GameWorld/Player1");

  // Assert: Should find Player1 node
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Player1");
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithDeepPath_FindsCorrectNode)
{
  // Arrange: Game scene with deep equipment paths
  CreateMultiPlayerHierarchy();

  // Act: Find equipment using deep absolute path
  auto result = query_->FindFirstByPath("GameWorld/Player1/Weapon");

  // Assert: Should find the weapon node
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "Weapon");
}

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithInvalidPath_ReturnsNullopt)
{
  // Arrange: Game scene loaded
  CreateGameSceneHierarchy();

  // Act: Try to find non-existent path
  auto result = query_->FindFirstByPath("GameWorld/NonExistent/Path");

  // Assert: Should return nullopt
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithEmptyPath_ReturnsNullopt)
{
  // Arrange: Game scene loaded
  CreateGameSceneHierarchy();

  // Act: Try to find with empty path
  auto result = query_->FindFirstByPath("");

  // Assert: Should return nullopt
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithRootPath_FindsRoot)
{
  // Arrange: Multi-player hierarchy with "GameWorld" root
  CreateMultiPlayerHierarchy();

  // Act: Find root node
  auto result = query_->FindFirstByPath("GameWorld");

  // Assert: Should find GameWorld root
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->GetName(), "GameWorld");
}

NOLINT_TEST_F(
  SceneQueryPathTest, FindFirstByPath_WithScopedTraversal_FindsWithinScope)
{
  // Arrange: Multi-player hierarchy for scoped path tests
  CreateMultiPlayerHierarchy();

  // Find Player1 subtree to scope
  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Find weapon within Player1 scope using relative path
  query_->AddToTraversalScope(*player1_node);
  auto scoped_weapon = query_->FindFirstByPath("Player1/Weapon");

  // Reset and try same path in full scope
  query_->ResetTraversalScope();
  auto full_scope_weapon = query_->FindFirstByPath("GameWorld/Player1/Weapon");

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
  using ::testing::AllOf;
  using ::testing::SizeIs;
  using ::testing::UnorderedElementsAre;

  // Arrange: Game scene with known structure
  CreateGameSceneHierarchy();
  std::vector<SceneNode> level1_children;

  // Act: Collect all direct children of Level1
  auto result = query_->CollectByPath(level1_children, "Level1/*");

  // Assert: Should collect Player, Enemies, Items
  EXPECT_TRUE(result.completed);

  // Extract node names for comparison
  std::vector<std::string> child_names;
  std::transform(level1_children.begin(), level1_children.end(),
    std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers
  EXPECT_THAT(child_names,
    AllOf(SizeIs(3), UnorderedElementsAre("Player", "Enemies", "Items")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSingleWildcard_CollectsDirectChildren)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::SizeIs;

  // Arrange: Game scene with Player having equipment
  CreateGameSceneHierarchy();
  std::vector<SceneNode> player_equipment;

  // Act: Collect all direct children of Player using wildcard
  auto result = query_->CollectByPath(player_equipment, "Level1/Player/*");

  // Assert: Should collect Player's equipment
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(player_equipment, SizeIs(2)); // Weapon, Shield
  // Extract node names for verification
  std::vector<std::string> equipment_names;
  std::transform(player_equipment.begin(), player_equipment.end(),
    std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain expected equipment types
  EXPECT_THAT(
    equipment_names, AllOf(SizeIs(2), Contains("Weapon"), Contains("Shield")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithRecursiveWildcard_CollectsAllDepths)
{
  using ::testing::AllOf;
  using ::testing::Each;
  using ::testing::Property;
  using ::testing::SizeIs;

  // Arrange: Game scene with weapons at different depths
  CreateGameSceneHierarchy();
  std::vector<SceneNode> weapons;

  // Act: Collect all Weapon nodes recursively using double wildcard
  auto result = query_->CollectByPath(weapons, "**/Weapon");

  // Assert: Should find the one weapon under Player
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(weapons, SizeIs(1)); // One weapon under Level1/Player

  // All collected nodes should be named "Weapon"
  EXPECT_THAT(weapons, Each(Property(&SceneNode::GetName, "Weapon")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithComplexPattern_CollectsCorrectly)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::SizeIs;

  // Arrange: Game scene with enemies under Level1
  CreateGameSceneHierarchy();
  std::vector<SceneNode> enemies;

  // Act: Collect all enemies using complex pattern
  auto result = query_->CollectByPath(enemies, "Level1/Enemies/*");

  // Assert: Should collect all 3 enemy nodes
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(enemies, SizeIs(3)); // Enemy1, Enemy2, Enemy3
  // Extract names for verification
  std::vector<std::string> enemy_names;
  std::transform(enemies.begin(), enemies.end(),
    std::back_inserter(enemy_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain all enemy nodes
  EXPECT_THAT(enemy_names,
    AllOf(
      SizeIs(3), Contains("Enemy1"), Contains("Enemy2"), Contains("Enemy3")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithNoMatches_ReturnsEmptyContainer)
{
  // Arrange: Game scene loaded
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Try to collect non-existent pattern
  auto result = query_->CollectByPath(nodes, "**/NonExistent");

  // Assert: Should return empty container
  EXPECT_TRUE(result.completed);
  EXPECT_TRUE(nodes.empty());
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithExactPath_CollectsSingleNode)
{
  using ::testing::SizeIs;

  // Arrange: Game scene with known exact path
  CreateGameSceneHierarchy();
  std::vector<SceneNode> nodes;

  // Act: Collect using exact path to single node
  auto result = query_->CollectByPath(nodes, "Level1/Player/Weapon");

  // Assert: Should collect exactly one node
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Weapon");
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithScopedTraversal_CollectsWithinScope)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::Not;
  using ::testing::SizeIs;

  // Arrange: Multi-player hierarchy for scoped collection
  CreateMultiPlayerHierarchy();

  // Find Player1 subtree to scope
  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  ASSERT_TRUE(player1_node.has_value());

  // Act: Collect all equipment within Player1 scope
  std::vector<SceneNode> scoped_equipment;
  query_->AddToTraversalScope(*player1_node);
  auto result = query_->CollectByPath(scoped_equipment, "Player1/*");

  // Assert: Should collect only Player1's equipment
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(scoped_equipment, SizeIs(3)); // Weapon, Shield, Armor
  // Extract names for verification
  std::vector<std::string> equipment_names;
  std::transform(scoped_equipment.begin(), scoped_equipment.end(),
    std::back_inserter(equipment_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Should contain Player1's equipment but not other players'
  EXPECT_THAT(equipment_names,
    AllOf(
      SizeIs(3), Contains("Weapon"), Contains("Shield"), Contains("Armor")));
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithMultipleScopedNodes_CollectsFromAll)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::Not;
  using ::testing::SizeIs;

  // Arrange: Multi-player hierarchy with multiple subtrees
  CreateMultiPlayerHierarchy();

  auto player1_node = query_->FindFirst(NodeNameEquals("Player1"));
  auto player2_node = query_->FindFirst(NodeNameEquals("Player2"));
  ASSERT_TRUE(player1_node.has_value());
  ASSERT_TRUE(player2_node.has_value());

  // Act: Add multiple nodes to traversal scope
  std::vector<SceneNode> scope_nodes = { *player1_node, *player2_node };
  query_->AddToTraversalScope(scope_nodes);

  std::vector<SceneNode> collected_weapons;
  auto result = query_->CollectByPath(collected_weapons, "*/Weapon");

  // Assert: Should collect weapons from both scoped players
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(collected_weapons, SizeIs(2)); // One weapon from each player

  // Extract names to verify all are weapons
  std::vector<std::string> weapon_names;
  std::transform(collected_weapons.begin(), collected_weapons.end(),
    std::back_inserter(weapon_names),
    [](const SceneNode& node) { return node.GetName(); });
  // All should be weapons, none from outside scope
  EXPECT_THAT(weapon_names,
    AllOf(SizeIs(2), Contains("Weapon"), Not(Contains("Merchant"))));
}

//=== Edge Cases and Complex Patterns ===----------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithNestedWildcards_WorksCorrectly)
{
  using ::testing::AllOf;
  using ::testing::Each;
  using ::testing::Property;
  using ::testing::SizeIs;

  // Arrange: Multi-player hierarchy with weapons at different depths
  CreateMultiPlayerHierarchy();
  std::vector<SceneNode> all_weapons;

  // Act: Find all Weapon nodes at any depth using nested wildcards
  auto result = query_->CollectByPath(all_weapons, "**/Weapon");

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
  using ::testing::AllOf;
  using ::testing::IsSupersetOf;
  using ::testing::SizeIs;

  // Arrange: Game scene with multiple root nodes
  CreateGameSceneHierarchy();
  std::vector<SceneNode> root_children;

  // Act: Collect direct children of any root using wildcard
  auto result = query_->CollectByPath(root_children, "*/*");

  // Assert: Should collect children from both Level1 and UI roots
  EXPECT_TRUE(result.completed);

  // Extract names for verification
  std::vector<std::string> child_names;
  std::transform(root_children.begin(), root_children.end(),
    std::back_inserter(child_names),
    [](const SceneNode& node) { return node.GetName(); });

  // Use Google Test collection matchers - should contain children from both
  // roots
  EXPECT_THAT(child_names,
    AllOf(SizeIs(6), // 3 from Level1 + 3 from UI
      IsSupersetOf({ "Player", "Enemies", "Items", "MainMenu", "HealthBar",
        "Inventory" })));
}

NOLINT_TEST_F(SceneQueryPathTest, CollectByPath_Performance_WithLargeHierarchy)
{
  using ::testing::SizeIs;

  // Arrange: Create large hierarchy for performance test
  CreateForestScene(10, 20); // 10 roots with 20 children each
  CreateQuery();
  std::vector<SceneNode> all_nodes;

  // Act: Collect all nodes using recursive wildcard
  auto result = query_->CollectByPath(all_nodes, "*/*");

  // Assert: Should complete successfully with many nodes
  EXPECT_TRUE(result.completed);
  EXPECT_THAT(
    all_nodes, SizeIs(200)); // 10 roots × 20 children = 200 child nodes
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithGlobalWildcard_CollectsAllNodes)
{
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::SizeIs;

  // Arrange: Multi-player hierarchy with known structure
  CreateMultiPlayerHierarchy();
  std::vector<SceneNode> all_nodes;

  // Act: Collect all nodes at any depth using global wildcard
  auto result = query_->CollectByPath(all_nodes, "**/*");

  // Assert: Should collect every node in the scene
  EXPECT_TRUE(result.completed);

  // Extract node names for verification
  std::vector<std::string> node_names;
  std::transform(all_nodes.begin(), all_nodes.end(),
    std::back_inserter(node_names),
    [](const SceneNode& node) { return node.GetName(); });
  // Should contain all nodes from the hierarchy (16 total nodes)
  EXPECT_THAT(node_names,
    AllOf(SizeIs(16), // GameWorld + 4 groups + 11 child nodes = 16 total
      Contains("GameWorld"), // root
      Contains("Player1"), // players
      Contains("Player2"),
      Contains("Weapon"), // equipment (appears twice - one per player)
      Contains("Shield"), // Player1 equipment
      Contains("Armor"), // Player1 equipment
      Contains("Bow"), // Player2 equipment
      Contains("Quiver"), // Player2 equipment
      Contains("NPCs"), // groups
      Contains("Environment"),
      Contains("Merchant"), // NPCs
      Contains("Guard"), // NPCs
      Contains("Tree1"), // Environment
      Contains("Tree2"), // Environment
      Contains("Rock"))); // Environment
}

//=== Error Conditions ===--------------------------------------------------//

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithEmptyScene_HandlesGracefully)
{
  // Arrange: Create empty scene
  scene_->Clear();
  ASSERT_TRUE(scene_->IsEmpty());
  CreateQuery();

  // Act: Perform path queries on empty scene
  auto find_result = query_->FindFirstByPath("Any/Path");
  std::vector<SceneNode> nodes;
  auto collect_result = query_->CollectByPath(nodes, "**/*");

  // Assert: All operations should complete gracefully
  EXPECT_FALSE(find_result.has_value());
  EXPECT_TRUE(collect_result.completed);
  EXPECT_TRUE(nodes.empty());
}

NOLINT_TEST_F(
  SceneQueryPathTest, CollectByPath_WithSingleNodeScene_WorksCorrectly)
{
  using ::testing::SizeIs;

  // Arrange: Create simple single node scene
  CreateSimpleScene();
  std::vector<SceneNode> nodes;

  // Act: Query the single node scene with path
  auto find_result = query_->FindFirstByPath("Root");
  auto collect_result = query_->CollectByPath(nodes, "Root");

  // Assert: Should handle single node correctly
  ASSERT_TRUE(find_result.has_value());
  EXPECT_EQ(find_result->GetName(), "Root");
  EXPECT_TRUE(collect_result.completed);
  EXPECT_THAT(nodes, SizeIs(1));
  EXPECT_EQ(nodes[0].GetName(), "Root");
}

} // namespace
