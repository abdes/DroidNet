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

  //! Create an empty scene
  void CreateEmptyScene();

  //! Create a simple single-node scene for basic testing
  void CreateSimpleScene();

  //! Create a parent-child hierarchy for basic relationship testing
  void CreateParentChildScene();

  //! Create a linear chain hierarchy (A -> B -> C -> D)
  void CreateLinearChainScene(int depth = 4);

  //! Create a binary tree hierarchy for complex traversal testing
  void CreateBinaryTreeScene(int depth = 3);

  //! Create a forest (multiple root nodes) for multi-root testing
  void CreateForestScene(int root_count = 3, int children_per_root = 2);

  //! Create multi-player hierarchy for scoped traversal testing
  void CreateMultiPlayerHierarchy();

  //=== Node Creation Helpers ===---------------------------------------------//

  //! Create a visible node with given name
  [[nodiscard]] SceneNode CreateVisibleNode(const std::string& name) const;

  //! Create an invisible node with given name
  [[nodiscard]] SceneNode CreateInvisibleNode(const std::string& name) const;

  //! Create a static node with given name
  [[nodiscard]] SceneNode CreateStaticNode(const std::string& name) const;

  //! Create a child node under the given parent
  [[nodiscard]] SceneNode CreateChildNode(
    SceneNode& parent, const std::string& name) const;

  //=== Query Helper Methods ===----------------------------------------------//
  //! Create a fresh query instance for the current scene
  void CreateQuery();
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
  void ExpectQueryResult(const QueryResult& result,
    std::size_t expected_examined, std::size_t expected_matched,
    bool expected_success) const;

  //! Expect BatchResult to have specific metrics
  void ExpectBatchResult(const BatchResult& result,
    std::size_t expected_examined, std::size_t expected_total_matches,
    bool expected_completed) const;

  //! Expect node to have specific name
  void ExpectNodeWithName(const std::optional<SceneNode>& node_opt,
    const std::string& expected_name) const;

  //! Expect container to contain nodes with specific names
  template <typename Container>
  void ExpectNodesWithNames(const Container& nodes,
    const std::vector<std::string>& expected_names) const;

  //=== Test Data Access ===--------------------------------------------------//

  //! Get reference to TestSceneFactory instance
  [[nodiscard]] TestSceneFactory& GetFactory();

  //! Get reference to TestSceneFactory instance (const)
  [[nodiscard]] const TestSceneFactory& GetFactory() const;

private:
  //=== JSON Templates for Complex Hierarchies ===---------------------------//

  //! Get JSON template for complex test hierarchy
  [[nodiscard]] static std::string GetComplexHierarchyJson();
  //! Get JSON template for game scene hierarchy
  [[nodiscard]] static std::string GetGameSceneJson();

  //! Get JSON template for multi-player hierarchy
  [[nodiscard]] static std::string GetMultiPlayerHierarchyJson();
};

//=== Template Implementation ===--------------------------------------------//

template <typename Container>
void SceneQueryTestBase::ExpectNodesWithNames(
  const Container& nodes, const std::vector<std::string>& expected_names) const
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
