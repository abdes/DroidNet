//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::ObjectMetaData;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::TransformComponent;

namespace {

//------------------------------------------------------------------------------
// Base fixture for all scene cloning tests
//------------------------------------------------------------------------------
class SceneCloningTestBase : public testing::Test {
protected:
  void SetUp() override
  {
    source_scene_ = std::make_shared<Scene>("SourceScene", 1024);
    target_scene_ = std::make_shared<Scene>("TargetScene", 1024);
  }

  void TearDown() override
  {
    source_scene_.reset();
    target_scene_.reset();
  }

  static void ExpectNodeValidWithName(
    const SceneNode& node, const std::string& expected_name)
  {
    EXPECT_TRUE(node.IsValid());
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    EXPECT_EQ(impl_opt->get().GetName(), expected_name);
  }

  static void SetTransformValues(
    SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    auto& transform = impl_opt->get().GetComponent<TransformComponent>();
    transform.SetLocalPosition(position);
    transform.SetLocalScale(scale);
  }

  static void ExpectTransformValues(const SceneNode& node,
    const glm::vec3& expected_position, const glm::vec3& expected_scale)
  {
    const auto impl_opt = node.GetObject();
    ASSERT_TRUE(impl_opt.has_value());
    const auto& transform = impl_opt->get().GetComponent<TransformComponent>();
    EXPECT_EQ(transform.GetLocalPosition(), expected_position);
    EXPECT_EQ(transform.GetLocalScale(), expected_scale);
  }

  std::shared_ptr<Scene> source_scene_;
  std::shared_ptr<Scene> target_scene_;
};

//------------------------------------------------------------------------------
// Single Node Cloning Tests (root or child, no hierarchy)
//------------------------------------------------------------------------------
class SceneSingleNodeCloningTest : public SceneCloningTestBase { };

NOLINT_TEST_F(
  SceneSingleNodeCloningTest, CloneSingleNode_CreatesValidCloneWithNewName)
{
  // Arrange
  const auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(original.IsValid())
    << "Original node should be valid after creation.";

  // Act
  const auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");

  // Assert
  EXPECT_TRUE(cloned.has_value());
  ExpectNodeValidWithName(*cloned, "ClonedNode");
  ExpectNodeValidWithName(original, "OriginalNode");
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  CloneSingleNodeWithinSameScene_ProducesIndependentNodes)
{
  // Arrange
  const auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(original.IsValid())
    << "Original node should be valid after creation.";

  // Act
  const auto cloned = source_scene_->CreateNodeFrom(original, "ClonedNode");

  // Assert
  EXPECT_TRUE(original.IsValid())
    << "Original node should remain valid after cloning.";
  EXPECT_TRUE(cloned.has_value());
  EXPECT_TRUE(cloned->IsValid()) << "Cloned node should be valid.";
  EXPECT_NE(original.GetHandle(), cloned->GetHandle())
    << "Handles must differ for independent nodes.";
  ExpectNodeValidWithName(original, "OriginalNode");
  ExpectNodeValidWithName(*cloned, "ClonedNode");
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  ClonesAreIndependent_ChangingOneDoesNotAffectOther)
{
  // Arrange
  auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(original.IsValid());
  SetTransformValues(original, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  // Act
  auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");
  EXPECT_TRUE(cloned.has_value());
  ASSERT_TRUE(cloned->IsValid());

  // Assert initial state
  ExpectTransformValues(*cloned, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  // Change original, assert clone unchanged
  const auto original_impl_opt = original.GetObject();
  ASSERT_TRUE(original_impl_opt.has_value());
  auto& original_impl = original_impl_opt->get();
  original_impl.SetName("ModifiedOriginal");
  SetTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });

  ExpectNodeValidWithName(*cloned, "ClonedNode");
  ExpectTransformValues(*cloned, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  // Change clone, assert original unchanged
  const auto cloned_impl_opt = cloned->GetObject();
  ASSERT_TRUE(cloned_impl_opt.has_value());
  auto& cloned_impl = cloned_impl_opt->get();
  cloned_impl.SetName("ModifiedClone");
  SetTransformValues(*cloned, { 100.0f, 200.0f, 300.0f }, { 3.0f, 3.0f, 3.0f });

  ExpectNodeValidWithName(original, "ModifiedOriginal");
  ExpectTransformValues(
    original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  CreateChildNodeFrom_ValidParentAndOriginal_CreatesChildClone)
{
  // Arrange
  const auto parent = target_scene_->CreateNode("Parent");
  const auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(original.IsValid());

  // Act
  const auto child_clone_opt
    = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");

  // Assert
  ASSERT_TRUE(child_clone_opt.has_value());
  const auto& child_clone = child_clone_opt.value();

  ExpectNodeValidWithName(child_clone, "ChildClone");
  EXPECT_FALSE(child_clone.IsRoot());
  EXPECT_TRUE(child_clone.HasParent());

  const auto parent_of_clone_opt = child_clone.GetParent();
  ASSERT_TRUE(parent_of_clone_opt.has_value());
  EXPECT_EQ(parent_of_clone_opt.value().GetHandle(), parent.GetHandle());

  const auto first_child_opt = parent.GetFirstChild();
  ASSERT_TRUE(first_child_opt.has_value());
  EXPECT_EQ(first_child_opt.value().GetHandle(), child_clone.GetHandle());
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  CreateChildNodeFrom_CrossSceneCloning_PreservesComponentData)
{
  // Arrange
  const auto parent = target_scene_->CreateNode("Parent");
  ASSERT_TRUE(parent.IsValid());

  auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(original.IsValid());
  SetTransformValues(original, { 5.0f, 10.0f, 15.0f }, { 2.0f, 3.0f, 4.0f });

  const auto original_impl_opt = original.GetObject();
  ASSERT_TRUE(original_impl_opt.has_value());
  auto& original_flags = original_impl_opt->get().GetFlags();
  original_flags.SetLocalValue(SceneNodeFlags::kVisible, false);
  original_flags.ProcessDirtyFlags();

  // Act
  const auto child_clone_opt
    = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");

  // Assert
  ASSERT_TRUE(child_clone_opt.has_value());
  auto child_clone = child_clone_opt.value();

  ExpectTransformValues(
    child_clone, { 5.0f, 10.0f, 15.0f }, { 2.0f, 3.0f, 4.0f });

  const auto clone_impl_opt = child_clone.GetObject();
  ASSERT_TRUE(clone_impl_opt.has_value());
  const auto& clone_flags = clone_impl_opt->get().GetFlags();
  EXPECT_EQ(clone_flags.GetEffectiveValue(SceneNodeFlags::kVisible), false);
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  CreateChildNodeFrom_SameSceneCloning_ProducesIndependentChild)
{
  // Arrange
  const auto parent = source_scene_->CreateNode("Parent");
  const auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(original.IsValid());
  EXPECT_TRUE(original.IsRoot());

  // Act
  const auto child_clone_opt
    = source_scene_->CreateChildNodeFrom(parent, original, "ChildClone");

  // Assert
  ASSERT_TRUE(child_clone_opt.has_value());
  const auto& child_clone = child_clone_opt.value();

  EXPECT_TRUE(original.IsValid());
  EXPECT_TRUE(child_clone.IsValid());
  EXPECT_NE(original.GetHandle(), child_clone.GetHandle());

  EXPECT_TRUE(original.IsRoot());
  EXPECT_FALSE(child_clone.IsRoot());

  const auto parent_of_clone_opt = child_clone.GetParent();
  ASSERT_TRUE(parent_of_clone_opt.has_value());
  EXPECT_EQ(parent_of_clone_opt.value().GetHandle(), parent.GetHandle());
}

NOLINT_TEST_F(SceneSingleNodeCloningTest,
  CreateChildNodeFrom_ClonedChildAndOriginalAreIndependent)
{
  // Arrange
  const auto parent = target_scene_->CreateNode("Parent");
  auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(original.IsValid());
  SetTransformValues(original, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  // Act
  const auto child_clone_opt
    = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");

  // Assert
  ASSERT_TRUE(child_clone_opt.has_value());
  auto child_clone = child_clone_opt.value();

  ExpectTransformValues(
    child_clone, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  SetTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
  ExpectTransformValues(
    child_clone, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

  SetTransformValues(
    child_clone, { 100.0f, 200.0f, 300.0f }, { 3.0f, 3.0f, 3.0f });
  ExpectTransformValues(
    original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
}

//------------------------------------------------------------------------------
// Hierarchy Cloning Tests (parent + children)
//------------------------------------------------------------------------------
class SceneHierarchyCloningTest : public SceneCloningTestBase { };

NOLINT_TEST_F(SceneHierarchyCloningTest,
  CloneHierarchy_NodesAreOrphaned_NoParentChildRelationship)
{
  // Arrange
  auto parent = source_scene_->CreateNode("Parent");
  auto child1_opt = source_scene_->CreateChildNode(parent, "Child1");
  auto child2_opt = source_scene_->CreateChildNode(parent, "Child2");

  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  const auto& child1 = child1_opt.value();
  const auto& child2 = child2_opt.value();

  EXPECT_TRUE(parent.GetFirstChild().has_value());
  EXPECT_TRUE(child1.GetParent().has_value());
  EXPECT_TRUE(child2.GetParent().has_value());

  // Act
  auto cloned_parent = target_scene_->CreateNodeFrom(parent, "ClonedParent");
  auto cloned_child1 = target_scene_->CreateNodeFrom(child1, "ClonedChild1");
  auto cloned_child2 = target_scene_->CreateNodeFrom(child2, "ClonedChild2");

  // Assert
  ASSERT_TRUE(cloned_parent.has_value());
  ASSERT_TRUE(cloned_parent->IsValid());
  ASSERT_TRUE(cloned_child1.has_value());
  ASSERT_TRUE(cloned_child1->IsValid());
  ASSERT_TRUE(cloned_child2.has_value());
  ASSERT_TRUE(cloned_child2->IsValid());

  EXPECT_FALSE(cloned_parent->GetFirstChild().has_value());
  EXPECT_FALSE(cloned_child1->GetParent().has_value());
  EXPECT_FALSE(cloned_child2->GetParent().has_value());

  EXPECT_TRUE(cloned_parent->IsRoot());
  EXPECT_TRUE(cloned_child1->IsRoot());
  EXPECT_TRUE(cloned_child2->IsRoot());
}

NOLINT_TEST_F(SceneHierarchyCloningTest,
  CreateChildNodeFrom_MultipleChildren_MaintainsParentChildHierarchy)
{
  // Arrange
  auto parent = target_scene_->CreateNode("Parent");
  auto original1 = source_scene_->CreateNode("Original1");
  auto original2 = source_scene_->CreateNode("Original2");
  ASSERT_TRUE(parent.IsValid());
  ASSERT_TRUE(original1.IsValid());
  ASSERT_TRUE(original2.IsValid());

  // Act
  auto child1_opt
    = target_scene_->CreateChildNodeFrom(parent, original1, "Child1");
  auto child2_opt
    = target_scene_->CreateChildNodeFrom(parent, original2, "Child2");

  // Assert
  ASSERT_TRUE(child1_opt.has_value());
  ASSERT_TRUE(child2_opt.has_value());
  const auto& child1 = child1_opt.value();
  const auto& child2 = child2_opt.value();

  EXPECT_EQ(child1.GetParent().value().GetHandle(), parent.GetHandle());
  EXPECT_EQ(child2.GetParent().value().GetHandle(), parent.GetHandle());

  auto children = target_scene_->GetChildren(parent);
  EXPECT_EQ(children.size(), 2);
  EXPECT_TRUE(
    std::ranges::find(children, child1.GetHandle()) != children.end());
  EXPECT_TRUE(
    std::ranges::find(children, child2.GetHandle()) != children.end());
}

//------------------------------------------------------------------------------
// Error/Death Tests
//------------------------------------------------------------------------------
class SceneCloningErrorTest : public SceneCloningTestBase { };

NOLINT_TEST_F(
  SceneCloningErrorTest, CreateChildNodeFrom_InvalidParent_TriggersDeath)
{
  // Arrange
  const auto original = source_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(original.IsValid());

  auto invalid_parent = target_scene_->CreateNode("ParentNode");
  target_scene_->DestroyNode(invalid_parent);
  EXPECT_FALSE(invalid_parent.IsValid());

  // Act: Attempt to clone from a destroyed node
  auto clone = target_scene_->CreateChildNodeFrom(
    invalid_parent, original, "ClonedChild");

  // Assert: Expect failure
  EXPECT_FALSE(clone.has_value());
}

NOLINT_TEST_F(SceneCloningErrorTest,
  CreateChildNodeFrom_ParentFromDifferentScene_TriggersDeath)
{
  // Arrange
  const auto parent_in_source = source_scene_->CreateNode("Parent");
  const auto original = target_scene_->CreateNode("OriginalNode");
  ASSERT_TRUE(parent_in_source.IsValid());
  ASSERT_TRUE(original.IsValid());

  // Act & Assert
  EXPECT_DEATH(
    {
      [[maybe_unused]] auto result = target_scene_->CreateChildNodeFrom(
        parent_in_source, original, "ChildClone");
    },
    ".*does not belong to scene.*");
}

NOLINT_TEST_F(SceneCloningErrorTest, CreateChildNodeFrom_InvalidOriginal_Fails)
{
  // Arrange
  const auto parent = target_scene_->CreateNode("Parent");
  ASSERT_TRUE(parent.IsValid());

  auto invalid_original = source_scene_->CreateNode("OriginalNode");
  source_scene_->DestroyNode(invalid_original);
  EXPECT_FALSE(invalid_original.IsValid());

  // Act: Attempt to clone from a destroyed node
  auto clone = target_scene_->CreateChildNodeFrom(
    parent, invalid_original, "ClonedChild");

  // Assert: Expect failure
  EXPECT_FALSE(clone.has_value());
}

} // namespace
