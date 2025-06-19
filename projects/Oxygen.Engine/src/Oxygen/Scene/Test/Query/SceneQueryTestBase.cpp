//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

void SceneQueryTestBase::SetUp()
{
  // Start with a basic scene that can be customized per test
  scene_ = GetFactory().CreateSingleNodeScene("TestScene");
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

void SceneQueryTestBase::TearDown()
{
  query_.reset();
  scene_.reset();

  // Reset factory state for clean test isolation
  GetFactory().Reset();
}

//=== Scene Creation Helpers ===--------------------------------------------//

auto SceneQueryTestBase::CreateEmptyScene() -> void
{
  scene_ = GetFactory().CreateEmptyScene("SimpleScene");
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateSimpleScene() -> void
{
  scene_ = GetFactory().CreateSingleNodeScene("SimpleScene");
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateParentChildScene() -> void
{
  scene_ = GetFactory().CreateParentChildScene("ParentChildScene");
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateLinearChainScene(int depth) -> void
{
  auto name_generator = std::make_unique<DefaultNameGenerator>();
  name_generator->SetPrefix("Node");
  GetFactory().SetNameGenerator<DefaultNameGenerator>(
    std::move(name_generator));
  scene_ = GetFactory().CreateLinearChainScene("LinearChainScene", depth);
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateBinaryTreeScene(int depth) -> void
{
  scene_ = GetFactory().CreateBinaryTreeScene("BinaryTreeScene", depth);
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateForestScene(
  int root_count, int children_per_root) -> void
{
  scene_ = GetFactory().CreateForestScene(
    "ForestScene", root_count, children_per_root);
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

auto SceneQueryTestBase::CreateMultiPlayerHierarchy() -> void
{
  const auto json = GetMultiPlayerHierarchyJson();
  scene_ = GetFactory().CreateFromJson(json, "MultiPlayerHierarchy");
  ASSERT_NE(scene_, nullptr);
  CreateQuery();
}

//=== Node Creation Helpers ===---------------------------------------------//

auto SceneQueryTestBase::CreateVisibleNode(const std::string& name) const
  -> SceneNode
{
  const auto flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true));
  auto node = scene_->CreateNode(name, flags);
  EXPECT_TRUE(node.IsValid()) << "Failed to create visible node: " << name;
  return node;
}

auto SceneQueryTestBase::CreateInvisibleNode(const std::string& name) const
  -> SceneNode
{
  const auto flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
  auto node = scene_->CreateNode(name, flags);
  EXPECT_TRUE(node.IsValid()) << "Failed to create invisible node: " << name;
  return node;
}

auto SceneQueryTestBase::CreateStaticNode(const std::string& name) const
  -> SceneNode
{
  const auto flags = SceneNode::Flags {}.SetFlag(
    SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));
  auto node = scene_->CreateNode(name, flags);
  EXPECT_TRUE(node.IsValid()) << "Failed to create static node: " << name;
  return node;
}

auto SceneQueryTestBase::CreateChildNode(
  SceneNode& parent, const std::string& name) const -> SceneNode
{
  auto child_opt = scene_->CreateChildNode(parent, name);
  EXPECT_TRUE(child_opt.has_value()) << "Failed to create child node: " << name;
  return child_opt.value_or(SceneNode {});
}

//=== Query Helper Methods ===----------------------------------------------//

auto SceneQueryTestBase::CreateQuery() -> void
{
  query_ = std::make_unique<SceneQuery>(scene_);
  ASSERT_NE(query_, nullptr);
}

//=== Assertion Helpers ===------------------------------------------------//

auto SceneQueryTestBase::ExpectQueryResult(const QueryResult& result,
  std::size_t expected_examined, std::size_t expected_matched,
  bool expected_completed) const -> void
{
  EXPECT_EQ(result.nodes_examined, expected_examined)
    << "Nodes examined mismatch";
  EXPECT_EQ(result.nodes_matched, expected_matched) << "Nodes matched mismatch";
  EXPECT_EQ(result.completed, expected_completed)
    << "Completion status mismatch";
}

auto SceneQueryTestBase::ExpectBatchResult(const BatchResult& result,
  std::size_t expected_examined, std::size_t expected_total_matches,
  bool expected_completed) const -> void
{
  EXPECT_EQ(result.nodes_examined, expected_examined)
    << "Batch nodes examined mismatch";
  EXPECT_EQ(result.total_matches, expected_total_matches)
    << "Batch total matches mismatch";
  EXPECT_EQ(result.completed, expected_completed)
    << "Batch completion status mismatch";
}

auto SceneQueryTestBase::ExpectNodeWithName(
  const std::optional<SceneNode>& node_opt,
  const std::string& expected_name) const -> void
{
  ASSERT_TRUE(node_opt.has_value()) << "Expected node but got nullopt";
  EXPECT_EQ(node_opt->GetName(), expected_name) << "Node name mismatch";
}

//=== Test Data Access ===--------------------------------------------------//

auto SceneQueryTestBase::GetFactory() -> TestSceneFactory&
{
  return TestSceneFactory::Instance();
}

auto SceneQueryTestBase::GetFactory() const -> const TestSceneFactory&
{
  return TestSceneFactory::Instance();
}

//=== JSON Templates for Complex Hierarchies ===---------------------------//

auto SceneQueryTestBase::GetComplexHierarchyJson() -> std::string
{
  return R"({
    "metadata": {
      "name": "ComplexHierarchy"
    },
    "nodes": [
      {
        "name": "World",
        "children": [
          {
            "name": "Environment",
            "children": [
              {"name": "Terrain"},
              {"name": "Sky"},
              {
                "name": "Buildings",
                "children": [
                  {"name": "House1"},
                  {"name": "House2"},
                  {"name": "Office"}
                ]
              }
            ]
          },
          {
            "name": "Characters",
            "children": [
              {
                "name": "Player",
                "children": [
                  {"name": "Equipment"},
                  {"name": "Inventory"}
                ]
              },
              {
                "name": "NPCs",
                "children": [
                  {"name": "Merchant"},
                  {"name": "Guard1"},
                  {"name": "Guard2"}
                ]
              }
            ]
          },
          {
            "name": "Effects",
            "children": [
              {"name": "ParticleSystem1"},
              {"name": "ParticleSystem2"}
            ]
          }
        ]
      }
    ]
  })";
}

auto SceneQueryTestBase::GetMultiPlayerHierarchyJson() -> std::string
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

} // namespace oxygen::scene::testing
