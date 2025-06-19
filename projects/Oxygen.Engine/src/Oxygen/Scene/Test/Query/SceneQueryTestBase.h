//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Test/Helpers/TestSceneFactory.h>

namespace oxygen::scene::testing {

//! Base test fixture for SceneQuery test suites providing common setup and
//! utilities
/*!
 Provides shared scene creation, query setup, and assertion helpers for all
 SceneQuery test fixtures. Uses TestSceneFactory for consistent scene creation
 across all test categories.

 Following TEST_INSTRUCTIONS.md guidelines for fixture-based testing with
 proper setup/teardown and reusable helper methods.
*/
class SceneQueryTestBase : public ::testing::Test {
public:
  std::shared_ptr<Scene> scene_;
  std::unique_ptr<SceneQuery> query_;

protected:
  //=== Fixture Management ===------------------------------------------------//

  void SetUp() override;
  void TearDown() override;

  //=== Scene Creation Helpers ===--------------------------------------------//

  //! Create a simple single-node scene for basic testing
  auto CreateSimpleScene() -> void;

  //! Create a parent-child hierarchy for basic relationship testing
  auto CreateParentChildScene() -> void;

  //! Create a linear chain hierarchy (A -> B -> C -> D)
  auto CreateLinearChainScene(int depth = 4) -> void;

  //! Create a binary tree hierarchy for complex traversal testing
  auto CreateBinaryTreeScene(int depth = 3) -> void;

  //! Create a forest (multiple root nodes) for multi-root testing
  auto CreateForestScene(int root_count = 3, int children_per_root = 2) -> void;

  //! Create complex hierarchy from JSON template
  auto CreateComplexHierarchyFromJson() -> void;

  //! Create game-like scene hierarchy for realistic testing
  auto CreateGameSceneHierarchy() -> void;

  //! Create multi-player hierarchy for scoped traversal testing
  auto CreateMultiPlayerHierarchy() -> void;

  //=== Node Creation Helpers ===---------------------------------------------//

  //! Create a visible node with given name
  [[nodiscard]] auto CreateVisibleNode(const std::string& name) const
    -> SceneNode;

  //! Create an invisible node with given name
  [[nodiscard]] auto CreateInvisibleNode(const std::string& name) const
    -> SceneNode;

  //! Create a static node with given name
  [[nodiscard]] auto CreateStaticNode(const std::string& name) const
    -> SceneNode;

  //! Create a child node under the given parent
  [[nodiscard]] auto CreateChildNode(
    SceneNode& parent, const std::string& name) const -> SceneNode;

  //=== Query Helper Methods ===----------------------------------------------//
  //! Create a fresh query instance for the current scene
  auto CreateQuery() -> void;
  //! Predicate helper: node name equals given string
  [[nodiscard]] static auto NodeNameEquals(const std::string& name)
  {
    return [name](const ConstVisitedNode& visited) -> bool {
      return visited.node_impl && visited.node_impl->GetName() == name;
    };
  }

  //! Predicate helper: node name starts with given prefix
  [[nodiscard]] static auto NodeNameStartsWith(const std::string& prefix)
  {
    return [prefix](const ConstVisitedNode& visited) -> bool {
      if (!visited.node_impl)
        return false;
      const auto node_name = visited.node_impl->GetName();
      return node_name.starts_with(prefix);
    };
  }

  //! Predicate helper: node is visible
  [[nodiscard]] static auto NodeIsVisible()
  {
    return [](const ConstVisitedNode& visited) -> bool {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    };
  }

  //! Predicate helper: node is invisible
  [[nodiscard]] static auto NodeIsInvisible()
  {
    return [](const ConstVisitedNode& visited) -> bool {
      return visited.node_impl
        && !visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kVisible);
    };
  }

  //! Predicate helper: node is static
  [[nodiscard]] static auto NodeIsStatic()
  {
    return [](const ConstVisitedNode& visited) -> bool {
      return visited.node_impl
        && visited.node_impl->GetFlags().GetEffectiveValue(
          SceneNodeFlags::kStatic);
    };
  }

  //=== Assertion Helpers ===------------------------------------------------//

  //! Expect QueryResult to have specific metrics
  auto ExpectQueryResult(const QueryResult& result,
    std::size_t expected_examined, std::size_t expected_matched,
    bool expected_completed) const -> void;

  //! Expect BatchResult to have specific metrics
  auto ExpectBatchResult(const BatchResult& result,
    std::size_t expected_examined, std::size_t expected_total_matches,
    bool expected_completed) const -> void;

  //! Expect node to have specific name
  auto ExpectNodeWithName(const std::optional<SceneNode>& node_opt,
    const std::string& expected_name) const -> void;

  //! Expect container to contain nodes with specific names
  template <typename Container>
  auto ExpectNodesWithNames(const Container& nodes,
    const std::vector<std::string>& expected_names) const -> void;

  //=== Test Data Access ===--------------------------------------------------//

  //! Get reference to TestSceneFactory instance
  [[nodiscard]] auto GetFactory() -> TestSceneFactory&;

  //! Get reference to TestSceneFactory instance (const)
  [[nodiscard]] auto GetFactory() const -> const TestSceneFactory&;

private:
  //=== JSON Templates for Complex Hierarchies ===---------------------------//

  //! Get JSON template for complex test hierarchy
  [[nodiscard]] static auto GetComplexHierarchyJson() -> std::string;
  //! Get JSON template for game scene hierarchy
  [[nodiscard]] static auto GetGameSceneJson() -> std::string;

  //! Get JSON template for multi-player hierarchy
  [[nodiscard]] static auto GetMultiPlayerHierarchyJson() -> std::string;
};

//=== Template Implementation ===--------------------------------------------//

template <typename Container>
auto SceneQueryTestBase::ExpectNodesWithNames(const Container& nodes,
  const std::vector<std::string>& expected_names) const -> void
{
  ASSERT_EQ(nodes.size(), expected_names.size()) << "Container size mismatch";

  auto node_it = nodes.begin();
  auto name_it = expected_names.begin();

  for (std::size_t i = 0; i < nodes.size(); ++i, ++node_it, ++name_it) {
    EXPECT_EQ(node_it->GetName(), *name_it)
      << "Node name mismatch at index " << i;
  }
}

} // namespace oxygen::scene::testing
