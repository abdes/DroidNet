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

  //=== Path-Based Query Test Fixture ===-------------------------------------//

  class SceneQueryPathTest : public SceneQueryTestBase {
  protected:
    void SetUp() override
    {
      // Create complex path-suitable hierarchy using JSON
      const auto json = R"({
      "metadata": {
        "name": "PathTestScene"
      },
      "nodes": [
        {
          "name": "World",
          "children": [
            {
              "name": "Level1",
              "children": [
                {
                  "name": "Player",
                  "children": [
                    {"name": "Equipment"},
                    {"name": "Weapon"},
                    {"name": "Shield"}
                  ]
                },
                {
                  "name": "Enemies",
                  "children": [
                    {"name": "Enemy1"},
                    {"name": "Enemy2"}
                  ]
                }
              ]
            },
            {
              "name": "Level2",
              "children": [
                {
                  "name": "Player",
                  "children": [
                    {"name": "Equipment"},
                    {"name": "Weapon"}
                  ]
                }
              ]
            }
          ]
        },
        {
          "name": "UI",
          "children": [
            {"name": "MainMenu"},
            {"name": "HealthBar"}
          ]
        }
      ]
    })";

      scene_ = GetFactory().CreateFromJson(json, "PathTestScene");
      ASSERT_NE(scene_, nullptr);
      CreateQuery();
    }
  };

  //=== FindFirstByPath Tests ===---------------------------------------------//

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithValidAbsolutePath_FindsNode)
  {
    // Arrange: Path to specific player in Level1

    // Act: Find player in Level1 using absolute path
    auto result = query_->FindFirstByPath("World/Level1/Player");

    // Assert: Should find the specific player node
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "Player");

    // Verify it's the correct player by checking parent
    auto parent = result->GetParent();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->GetName(), "Level1");
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithValidRelativePath_FindsFromContext)
  {
    // Arrange: Get Level1 as context node
    auto level1 = query_->FindFirst(NodeNameEquals("Level1"));
    ASSERT_TRUE(level1.has_value());

    // Act: Find player relative to Level1
    auto result = query_->FindFirstByPath(level1.value(), "Player");

    // Assert: Should find player under Level1
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "Player");
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithDeepPath_FindsCorrectNode)
  {
    // Arrange: Deep path to equipment

    // Act: Find equipment using deep absolute path
    auto result = query_->FindFirstByPath("World/Level1/Player/Equipment");

    // Assert: Should find the equipment node
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "Equipment");
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithInvalidPath_ReturnsNullopt)
  {
    // Arrange: Invalid path

    // Act: Try to find non-existent path
    auto result = query_->FindFirstByPath("World/NonExistent/Path");

    // Assert: Should return nullopt
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithEmptyPath_ReturnsNullopt)
  {
    // Arrange: Empty path

    // Act: Try to find with empty path
    auto result = query_->FindFirstByPath("");

    // Assert: Should return nullopt
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, FindFirstByPath_WithInvalidContext_ReturnsNullopt)
  {
    // Arrange: Create invalid context node
    SceneNode invalid_context;

    // Act: Try to find with invalid context
    auto result = query_->FindFirstByPath(invalid_context, "Player");

    // Assert: Should return nullopt
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(SceneQueryPathTest, FindFirstByPath_WithRootPath_FindsRoot)
  {
    // Arrange: Path to root

    // Act: Find root node
    auto result = query_->FindFirstByPath("World");

    // Assert: Should find World root
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetName(), "World");
  }

  //=== CollectByPath Tests ==================================================//

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithSimplePattern_CollectsMatches)
  {
    // Arrange: Pattern to collect all UI children
    std::vector<SceneNode> ui_children;

    // Act: Collect all direct children of UI
    auto result = query_->CollectByPath(ui_children, "UI/*");

    // Assert: Should collect MainMenu and HealthBar
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(ui_children.size(), 2);
    ExpectNodesWithNames(ui_children, { "MainMenu", "HealthBar" });
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithSingleWildcard_CollectsDirectChildren)
  {
    // Arrange: Pattern to collect all player children in any level
    std::vector<SceneNode> player_children;

    // Act: Collect all direct children of any Player
    auto result = query_->CollectByPath(player_children, "*/Player/*");

    // Assert: Should collect equipment from both levels
    EXPECT_TRUE(result.completed);
    EXPECT_GT(player_children.size(), 0);

    // Should find Equipment nodes under different Players
    bool found_equipment = false;
    for (const auto& child : player_children) {
      if (child.GetName() == "Equipment") {
        found_equipment = true;
        break;
      }
    }
    EXPECT_TRUE(found_equipment);
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithRecursiveWildcard_CollectsAllDepths)
  {
    // Arrange: Pattern to collect all Weapon nodes at any depth
    std::vector<SceneNode> weapons;

    // Act: Collect all Weapon nodes recursively
    auto result = query_->CollectByPath(weapons, "**/Weapon");

    // Assert: Should find weapons in both Level1 and Level2
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(weapons.size(), 2); // One in Level1, one in Level2

    for (const auto& weapon : weapons) {
      EXPECT_EQ(weapon.GetName(), "Weapon");
    }
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithComplexPattern_CollectsCorrectly)
  {
    // Arrange: Complex pattern mixing exact names and wildcards
    std::vector<SceneNode> level_children;

    // Act: Collect all children of any Level node
    auto result = query_->CollectByPath(level_children, "World/Level*/*");

    // Assert: Should collect Player and Enemies from both levels
    EXPECT_TRUE(result.completed);
    EXPECT_GT(level_children.size(), 2);

    // Should find both Player and Enemies nodes
    bool found_player = false, found_enemies = false;
    for (const auto& child : level_children) {
      if (child.GetName() == "Player")
        found_player = true;
      if (child.GetName() == "Enemies")
        found_enemies = true;
    }
    EXPECT_TRUE(found_player);
    EXPECT_TRUE(found_enemies);
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithNoMatches_ReturnsEmptyContainer)
  {
    // Arrange: Pattern that won't match anything
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
    // Arrange: Exact path to single node
    std::vector<SceneNode> nodes;

    // Act: Collect using exact path
    auto result = query_->CollectByPath(nodes, "World/Level1/Player/Weapon");

    // Assert: Should collect exactly one node
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0].GetName(), "Weapon");
  }

  //=== Edge Cases and Complex Patterns ====================================//

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithNestedWildcards_WorksCorrectly)
  {
    // Arrange: Pattern with multiple wildcards
    std::vector<SceneNode> equipment_items;

    // Act: Find all equipment items anywhere under World
    auto result = query_->CollectByPath(equipment_items, "World/**/Player/*");

    // Assert: Should find all items under any Player
    EXPECT_TRUE(result.completed);
    EXPECT_GT(equipment_items.size(), 0);

    // Should include Equipment, Weapon, Shield from Level1 and Equipment,
    // Weapon from Level2
    EXPECT_GE(equipment_items.size(), 5);
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_WithRootWildcard_CollectsFromAllRoots)
  {
    // Arrange: Pattern starting with wildcard to match any root
    std::vector<SceneNode> root_children;

    // Act: Collect direct children of any root
    auto result = query_->CollectByPath(root_children, "*/");

    // Assert: Should collect children from both World and UI roots
    EXPECT_TRUE(result.completed);
    EXPECT_GT(root_children.size(), 0);

    // Should find Level1, Level2 from World and MainMenu, HealthBar from UI
    bool found_level = false, found_ui_child = false;
    for (const auto& child : root_children) {
      if (child.GetName().starts_with("Level"))
        found_level = true;
      if (child.GetName() == "MainMenu" || child.GetName() == "HealthBar")
        found_ui_child = true;
    }
    EXPECT_TRUE(found_level);
    EXPECT_TRUE(found_ui_child);
  }

  NOLINT_TEST_F(
    SceneQueryPathTest, CollectByPath_Performance_WithLargeHierarchy)
  {
    // Arrange: Create large hierarchy for performance test
    CreateForestScene(10, 20); // 10 roots with 20 children each
    CreateQuery();
    std::vector<SceneNode> all_children;

    // Act: Collect all children using recursive wildcard
    auto result = query_->CollectByPath(all_children, "**/");

    // Assert: Should complete successfully with many nodes
    EXPECT_TRUE(result.completed);
    EXPECT_GT(all_children.size(), 200); // Should find many nodes
  }

} // namespace

} // namespace oxygen::scene::testing
