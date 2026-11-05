//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

namespace {

//! A table this small moves every node as soon as nodes are added, which is
//! what a scene does at any size once it grows past its capacity.
constexpr std::size_t kTinyCapacity = 2U;
constexpr std::size_t kGrownCount = 64U;

class SceneTableGrowthTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    scene_ = std::make_shared<Scene>("Growth", kTinyCapacity);
  }

  //! The names of a node's direct children, walked from the node itself.
  [[nodiscard]] static auto ChildNames(SceneNode node)
    -> std::multiset<std::string>
  {
    auto names = std::multiset<std::string> {};
    for (auto child = node.GetFirstChild(); child.has_value();
      child = child->GetNextSibling()) {
      names.insert(child->GetName());
    }
    return names;
  }

  std::shared_ptr<Scene> scene_;
};

//! A parent held across the insert that grows the table must still learn of
//! its child.
NOLINT_TEST_F(SceneTableGrowthTest, ChildrenStayLinkedWhileTheTableGrows)
{
  auto parent = scene_->CreateNode("parent");
  auto expected = std::multiset<std::string> {};
  for (std::size_t i = 0U; i < kGrownCount; ++i) {
    const auto name = "child" + std::to_string(i);
    auto child = scene_->CreateChildNode(parent, name);
    ASSERT_HAS_VALUE(child);
    expected.insert(name);
    auto child_parent = child->GetParent();
    ASSERT_HAS_VALUE(child_parent);
    EXPECT_EQ(child_parent->GetName(), "parent");
  }

  EXPECT_EQ(ChildNames(parent), expected);
  scene_->Update();
}

NOLINT_TEST_F(SceneTableGrowthTest, ClonedChildrenStayLinkedWhileTheTableGrows)
{
  auto parent = scene_->CreateNode("parent");
  auto original = scene_->CreateNode("original");
  auto expected = std::multiset<std::string> {};
  for (std::size_t i = 0U; i < kGrownCount; ++i) {
    const auto name = "clone" + std::to_string(i);
    const auto clone = scene_->CreateChildNodeFrom(parent, original, name);
    ASSERT_HAS_VALUE(clone);
    expected.insert(name);
  }

  EXPECT_EQ(ChildNames(parent), expected);
  scene_->Update();
}

//! Cloning a hierarchy of the same scene inserts into the table it reads: the
//! clone must have all of the original's nodes, however far the table moved.
NOLINT_TEST_F(SceneTableGrowthTest, HierarchyClonedIntoItsOwnSceneIsComplete)
{
  auto root = scene_->CreateNode("root");
  auto expected = std::multiset<std::string> {};
  for (std::size_t i = 0U; i < kGrownCount; ++i) {
    const auto name = "leaf" + std::to_string(i);
    const auto leaf = scene_->CreateChildNode(root, name);
    ASSERT_HAS_VALUE(leaf);
    expected.insert(name);
  }

  auto clone = scene_->CreateHierarchyFrom(root, "clone");
  ASSERT_TRUE(clone.IsValid());

  EXPECT_EQ(clone.GetName(), "clone");
  EXPECT_FALSE(clone.HasParent());
  EXPECT_EQ(ChildNames(clone), expected);
  EXPECT_EQ(ChildNames(root), expected);
  scene_->Update();
}

NOLINT_TEST_F(SceneTableGrowthTest, ChildHierarchyStaysLinkedWhileTheTableGrows)
{
  auto parent = scene_->CreateNode("parent");
  auto source = scene_->CreateNode("source");
  auto expected_leaves = std::multiset<std::string> {};
  for (std::size_t i = 0U; i < kGrownCount; ++i) {
    const auto name = "leaf" + std::to_string(i);
    const auto leaf = scene_->CreateChildNode(source, name);
    ASSERT_HAS_VALUE(leaf);
    expected_leaves.insert(name);
  }

  auto clone = scene_->CreateChildHierarchyFrom(parent, source, "clone");
  ASSERT_HAS_VALUE(clone);

  auto clone_parent = clone->GetParent();
  ASSERT_HAS_VALUE(clone_parent);
  EXPECT_EQ(clone_parent->GetName(), "parent");
  EXPECT_EQ(ChildNames(parent), std::multiset<std::string> { "clone" });
  EXPECT_EQ(ChildNames(*clone), expected_leaves);
  scene_->Update();
}

} // namespace
