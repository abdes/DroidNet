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
using oxygen::ResourceHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneCloningNodesTest : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Create source and target scenes for cross-scene cloning tests
        source_scene_ = std::make_shared<Scene>("SourceScene", 1024);
        target_scene_ = std::make_shared<Scene>("TargetScene", 1024);
    }

    void TearDown() override
    {
        // Clean up: Reset scene pointers to ensure proper cleanup
        source_scene_.reset();
        target_scene_.reset();
    }

    // Helper: Verify node has expected name and is valid
    static void ExpectNodeValidWithName(const SceneNode& node, const std::string& expected_name)
    {
        EXPECT_TRUE(node.IsValid());
        const auto impl_opt = node.GetObject();
        ASSERT_TRUE(impl_opt.has_value());
        EXPECT_EQ(impl_opt->get().GetName(), expected_name);
    }

    // Helper: Set transform values for testing component preservation
    static void SetTransformValues(SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
    {
        const auto impl_opt = node.GetObject();
        ASSERT_TRUE(impl_opt.has_value());
        auto& transform = impl_opt->get().GetComponent<TransformComponent>();
        transform.SetLocalPosition(position);
        transform.SetLocalScale(scale);
    }

    // Helper: Verify transform values match expected values
    static void ExpectTransformValues(const SceneNode& node, const glm::vec3& expected_position, const glm::vec3& expected_scale)
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
// CreateNodeFrom Basic Functionality Tests
//------------------------------------------------------------------------------

class SceneCreateNodeFromTest : public SceneCloningNodesTest { };

NOLINT_TEST_F(SceneCreateNodeFromTest, BasicCloning_CreatesValidCloneWithNewName)
{
    // Arrange: Create original node in source scene
    const auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    // Act: Clone node to target scene with new name
    const auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");

    // Assert: Verify cloned node is valid and has correct name
    ExpectNodeValidWithName(cloned, "ClonedNode");

    // Assert: Verify original node is unchanged
    ExpectNodeValidWithName(original, "OriginalNode");
}

NOLINT_TEST_F(SceneCreateNodeFromTest, SameSceneCloning_CreatesIndependentNodes)
{
    // Arrange: Create original node in source scene
    const auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    // Act: Clone node within the same scene
    const auto cloned = source_scene_->CreateNodeFrom(original, "ClonedNode");

    // Assert: Verify both nodes exist in the same scene with different handles
    EXPECT_TRUE(original.IsValid());
    EXPECT_TRUE(cloned.IsValid());
    EXPECT_NE(original.GetHandle(), cloned.GetHandle());

    // Assert: Verify both nodes have correct names
    ExpectNodeValidWithName(original, "OriginalNode");
    ExpectNodeValidWithName(cloned, "ClonedNode");
}

//------------------------------------------------------------------------------
// Data Preservation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneCreateNodeFromTest, ComponentDataPreservation_TransformAndFlagsPreserved)
{
    // Arrange: Create original node and modify its components
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    const auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    auto& original_impl = original_impl_opt->get();

    // Arrange: Modify transform component with specific values
    auto& transform = original_impl.GetComponent<TransformComponent>();
    transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f });
    transform.SetLocalRotation({ 0.707f, 0.707f, 0.0f, 0.0f }); // 90 degree rotation
    transform.SetLocalScale({ 2.0f, 1.5f, 0.5f });

    // Arrange: Modify flags with specific values
    auto& flags = original_impl.GetFlags();
    flags.SetLocalValue(SceneNodeFlags::kVisible, false);
    flags.SetLocalValue(SceneNodeFlags::kCastsShadows, true);
    flags.ProcessDirtyFlags(); // Apply pending flag changes

    // Act: Clone the node to preserve all component data
    auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");
    ASSERT_TRUE(cloned.IsValid());

    const auto cloned_impl_opt = cloned.GetObject();
    ASSERT_TRUE(cloned_impl_opt.has_value());
    auto& cloned_impl = cloned_impl_opt->get();

    // Assert: Verify transform component data is preserved exactly
    const auto& cloned_transform = cloned_impl.GetComponent<TransformComponent>();
    EXPECT_EQ(cloned_transform.GetLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(cloned_transform.GetLocalScale(), glm::vec3(2.0f, 1.5f, 0.5f));

    // Assert: Verify flags are preserved exactly
    const auto& cloned_flags = cloned_impl.GetFlags();
    EXPECT_EQ(cloned_flags.GetEffectiveValue(SceneNodeFlags::kVisible), false);
    EXPECT_EQ(cloned_flags.GetEffectiveValue(SceneNodeFlags::kCastsShadows), true);
}

NOLINT_TEST_F(SceneCreateNodeFromTest, ObjectMetaDataPreservation_MetaDataComponentExists)
{
    // Arrange: Create original node (ObjectMetaData should exist by default)
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    const auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    const auto& original_impl = original_impl_opt->get();

    // Arrange: Verify ObjectMetaData exists on original
    EXPECT_TRUE(original_impl.HasComponent<ObjectMetaData>());

    // Act: Clone the node
    auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");
    ASSERT_TRUE(cloned.IsValid());

    const auto cloned_impl_opt = cloned.GetObject();
    ASSERT_TRUE(cloned_impl_opt.has_value());
    const auto& cloned_impl = cloned_impl_opt->get();

    // Assert: Verify ObjectMetaData is preserved in clone
    EXPECT_TRUE(cloned_impl.HasComponent<ObjectMetaData>());
}

//------------------------------------------------------------------------------
// Independence Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneCreateNodeFromTest, ClonesAreIndependent_ModificationsDoNotAffectEachOther)
{
    // Arrange: Create original node and set initial transform values
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());
    SetTransformValues(original, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Clone the node
    auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");
    ASSERT_TRUE(cloned.IsValid());

    // Assert: Verify initial data is copied correctly
    ExpectTransformValues(cloned, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Modify original node (name and transform)
    const auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    auto& original_impl = original_impl_opt->get();
    original_impl.SetName("ModifiedOriginal");
    SetTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });

    // Assert: Verify clone is unaffected by original modifications
    ExpectNodeValidWithName(cloned, "ClonedNode");
    ExpectTransformValues(cloned, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Modify clone node (name and transform)
    const auto cloned_impl_opt = cloned.GetObject();
    ASSERT_TRUE(cloned_impl_opt.has_value());
    auto& cloned_impl = cloned_impl_opt->get();
    cloned_impl.SetName("ModifiedClone");
    SetTransformValues(cloned, { 100.0f, 200.0f, 300.0f }, { 3.0f, 3.0f, 3.0f });

    // Assert: Verify original is unaffected by clone modifications
    ExpectNodeValidWithName(original, "ModifiedOriginal");
    ExpectTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
}

NOLINT_TEST_F(SceneCreateNodeFromTest, ClonesAreOrphaned_NoHierarchyRelationshipsPreserved)
{
    // Arrange: Create a parent-child hierarchy in source scene
    auto parent = source_scene_->CreateNode("Parent");
    auto child1_opt = source_scene_->CreateChildNode(parent, "Child1");
    auto child2_opt = source_scene_->CreateChildNode(parent, "Child2");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    const auto& child1 = child1_opt.value();
    const auto& child2 = child2_opt.value();

    // Arrange: Verify original hierarchy exists
    EXPECT_TRUE(parent.GetFirstChild().has_value());
    EXPECT_TRUE(child1.GetParent().has_value());
    EXPECT_TRUE(child2.GetParent().has_value());

    // Act: Clone all nodes individually (clones are orphaned by design)
    auto cloned_parent = target_scene_->CreateNodeFrom(parent, "ClonedParent");
    auto cloned_child1 = target_scene_->CreateNodeFrom(child1, "ClonedChild1");
    auto cloned_child2 = target_scene_->CreateNodeFrom(child2, "ClonedChild2");

    ASSERT_TRUE(cloned_parent.IsValid());
    ASSERT_TRUE(cloned_child1.IsValid());
    ASSERT_TRUE(cloned_child2.IsValid());

    // Assert: Cloned nodes should have no hierarchy relationships (orphaned)
    EXPECT_FALSE(cloned_parent.GetFirstChild().has_value());
    EXPECT_FALSE(cloned_child1.GetParent().has_value());
    EXPECT_FALSE(cloned_child2.GetParent().has_value());

    // Assert: Cloned nodes should be root nodes in target scene
    EXPECT_TRUE(cloned_parent.IsRoot());
    EXPECT_TRUE(cloned_child1.IsRoot());
    EXPECT_TRUE(cloned_child2.IsRoot());
}

//------------------------------------------------------------------------------
// CreateChildNodeFrom Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_BasicFunctionality_CreatesValidChildClone)
{
    // Arrange: Create parent in target scene and original node in source scene
    const auto parent = target_scene_->CreateNode("Parent");
    const auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(parent.IsValid());
    ASSERT_TRUE(original.IsValid());

    // Act: Clone original as child of parent (using target scene instance method)
    const auto child_clone_opt = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");

    // Assert: Child clone should be created successfully
    ASSERT_TRUE(child_clone_opt.has_value());
    const auto& child_clone = child_clone_opt.value();

    // Assert: Verify child clone properties
    ExpectNodeValidWithName(child_clone, "ChildClone");
    EXPECT_FALSE(child_clone.IsRoot());
    EXPECT_TRUE(child_clone.HasParent());

    // Assert: Verify parent-child relationship
    const auto parent_of_clone_opt = child_clone.GetParent();
    ASSERT_TRUE(parent_of_clone_opt.has_value());
    EXPECT_EQ(parent_of_clone_opt.value().GetHandle(), parent.GetHandle());

    const auto first_child_opt = parent.GetFirstChild();
    ASSERT_TRUE(first_child_opt.has_value());
    EXPECT_EQ(first_child_opt.value().GetHandle(), child_clone.GetHandle());
}

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_CrossSceneCloning_PreservesComponentData)
{
    // Arrange: Create parent in target scene
    const auto parent = target_scene_->CreateNode("Parent");
    ASSERT_TRUE(parent.IsValid());

    // Arrange: Create original in source scene with specific component data
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());
    SetTransformValues(original, { 5.0f, 10.0f, 15.0f }, { 2.0f, 3.0f, 4.0f });

    // Arrange: Set flags on original
    const auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    auto& original_flags = original_impl_opt->get().GetFlags();
    original_flags.SetLocalValue(SceneNodeFlags::kVisible, false);
    original_flags.ProcessDirtyFlags();

    // Act: Clone original as child (using target scene instance method)
    const auto child_clone_opt = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");
    ASSERT_TRUE(child_clone_opt.has_value());
    auto child_clone = child_clone_opt.value();

    // Assert: Verify component data is preserved
    ExpectTransformValues(child_clone, { 5.0f, 10.0f, 15.0f }, { 2.0f, 3.0f, 4.0f });

    const auto clone_impl_opt = child_clone.GetObject();
    ASSERT_TRUE(clone_impl_opt.has_value());
    const auto& clone_flags = clone_impl_opt->get().GetFlags();
    EXPECT_EQ(clone_flags.GetEffectiveValue(SceneNodeFlags::kVisible), false);
}

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_SameSceneCloning_WorksCorrectly)
{
    // Arrange: Create parent and original in same scene
    const auto parent = source_scene_->CreateNode("Parent");
    const auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(parent.IsValid());
    ASSERT_TRUE(original.IsValid());

    // Act: Clone original as child within same scene (using source scene instance method)
    const auto child_clone_opt = source_scene_->CreateChildNodeFrom(parent, original, "ChildClone");
    ASSERT_TRUE(child_clone_opt.has_value());
    const auto& child_clone = child_clone_opt.value();

    // Assert: Verify both nodes exist and are independent
    EXPECT_TRUE(original.IsValid());
    EXPECT_TRUE(child_clone.IsValid());
    EXPECT_NE(original.GetHandle(), child_clone.GetHandle());

    // Assert: Verify original remains as root, clone is child
    EXPECT_TRUE(original.IsRoot());
    EXPECT_FALSE(child_clone.IsRoot());

    // Assert: Verify parent-child relationship
    const auto parent_of_clone_opt = child_clone.GetParent();
    ASSERT_TRUE(parent_of_clone_opt.has_value());
    EXPECT_EQ(parent_of_clone_opt.value().GetHandle(), parent.GetHandle());
}

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_MultipleChildren_MaintainsHierarchy)
{
    // Arrange: Create parent in target scene and multiple originals in source scene
    auto parent = target_scene_->CreateNode("Parent");
    auto original1 = source_scene_->CreateNode("Original1");
    auto original2 = source_scene_->CreateNode("Original2");
    ASSERT_TRUE(parent.IsValid());
    ASSERT_TRUE(original1.IsValid());
    ASSERT_TRUE(original2.IsValid());

    // Act: Clone multiple children (using target scene instance method)
    auto child1_opt = target_scene_->CreateChildNodeFrom(parent, original1, "Child1");
    auto child2_opt = target_scene_->CreateChildNodeFrom(parent, original2, "Child2");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    const auto& child1 = child1_opt.value();
    const auto& child2 = child2_opt.value();

    // Assert: Verify both children belong to parent
    EXPECT_EQ(child1.GetParent().value().GetHandle(), parent.GetHandle());
    EXPECT_EQ(child2.GetParent().value().GetHandle(), parent.GetHandle());

    // Assert: Verify parent has children (order may vary due to linking strategy)
    auto children = target_scene_->GetChildren(parent);
    EXPECT_EQ(children.size(), 2);
    EXPECT_TRUE(std::ranges::find(children, child1.GetHandle()) != children.end());
    EXPECT_TRUE(std::ranges::find(children, child2.GetHandle()) != children.end());
}

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_ClonesAreIndependent_ModificationsDoNotAffect)
{
    // Arrange: Create parent in target scene and original in source scene with initial data
    const auto parent = target_scene_->CreateNode("Parent");
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(parent.IsValid());
    ASSERT_TRUE(original.IsValid());
    SetTransformValues(original, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Clone as child (using target scene instance method)
    const auto child_clone_opt = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");
    ASSERT_TRUE(child_clone_opt.has_value());
    auto child_clone = child_clone_opt.value();

    // Assert: Verify initial data is copied
    ExpectTransformValues(child_clone, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Modify original
    SetTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });

    // Assert: Clone should be unaffected
    ExpectTransformValues(child_clone, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Modify clone
    SetTransformValues(child_clone, { 100.0f, 200.0f, 300.0f }, { 3.0f, 3.0f, 3.0f });

    // Assert: Original should be unaffected
    ExpectTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
}

//------------------------------------------------------------------------------
// CreateChildNodeFrom Error Scenarios
//------------------------------------------------------------------------------

class SceneCreateChildNodeFromTest : public SceneCloningNodesTest { };

NOLINT_TEST_F(SceneCreateChildNodeFromTest, CreateChildNodeFrom_InvalidParent_TerminatesProgram)
{
    // Arrange: Create original node and invalid parent
    const auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    // Create a valid parent node, then destroy it to make it invalid
    auto invalid_parent = target_scene_->CreateNode("ParentNode");
    target_scene_->DestroyNode(invalid_parent);
    EXPECT_FALSE(invalid_parent.IsValid());

    // Act & Assert: Should terminate program due to CHECK_F for invalid parent
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto result
                = target_scene_->CreateChildNodeFrom(invalid_parent, original, "ChildClone");
        },
        "expecting a valid parent node handle");
}

NOLINT_TEST_F(SceneCreateChildNodeFromTest, CreateChildNodeFrom_ParentFromDifferentScene_TerminatesProgram)
{
    // Arrange: Create parent in source scene and original in target scene
    const auto parent_in_source = source_scene_->CreateNode("Parent");
    const auto original = target_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(parent_in_source.IsValid());
    ASSERT_TRUE(original.IsValid());

    // Act & Assert: Should terminate program due to CHECK_F for cross-scene parent
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto result
                = target_scene_->CreateChildNodeFrom(parent_in_source, original, "ChildClone");
        },
        "expecting parent node to belong to this scene");
}

NOLINT_TEST_F(SceneCloningNodesTest, CreateChildNodeFrom_InvalidOriginal_TerminatesProgram)
{
    // Arrange: Create valid parent and invalid original
    const auto parent = target_scene_->CreateNode("Parent");
    ASSERT_TRUE(parent.IsValid());

    // Create a valid original node, then destroy it to make it invalid
    auto invalid_original = source_scene_->CreateNode("OriginalNode");
    source_scene_->DestroyNode(invalid_original);
    EXPECT_FALSE(invalid_original.IsValid());

    // Act & Assert: Should terminate program due to CHECK_F in CreateNodeFrom
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto result
                = target_scene_->CreateChildNodeFrom(parent, invalid_original, "ChildClone");
        },
        "expecting a valid original node handle");
}

NOLINT_TEST_F(SceneCreateChildNodeFromTest, CreateChildNodeFrom_OriginalRemovedFromScene_TerminatesProgram)
{
    // Arrange: Create parent and original
    const auto parent = target_scene_->CreateNode("Parent");
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(parent.IsValid());
    ASSERT_TRUE(original.IsValid());
    // Act: Remove original from scene
    const bool destroyed = source_scene_->DestroyNode(original);
    EXPECT_TRUE(destroyed);

    // Act & Assert: Should terminate program due to CHECK_F for invalid handle
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto result
                = target_scene_->CreateChildNodeFrom(parent, original, "ChildClone");
        },
        "expecting a valid original node handle");
}

NOLINT_TEST_F(SceneCreateChildNodeFromTest, CreateChildNodeFrom_CloningFromDestroyedHierarchy_HandlesGracefully)
{
    // Arrange: Create hierarchy in source scene
    auto source_parent = source_scene_->CreateNode("SourceParent");
    const auto source_child_opt = source_scene_->CreateChildNode(source_parent, "SourceChild");
    ASSERT_TRUE(source_child_opt.has_value());
    const auto& source_child = source_child_opt.value();

    // Arrange: Create target parent
    const auto target_parent = target_scene_->CreateNode("TargetParent");
    ASSERT_TRUE(target_parent.IsValid());

    // Act: Destroy the source hierarchy
    const bool destroyed = source_scene_->DestroyNodeHierarchy(source_parent);
    EXPECT_TRUE(destroyed);

    // Act & Assert: Attempting to clone destroyed child should terminate program
    EXPECT_DEATH(
        {
            [[maybe_unused]] auto result
                = target_scene_->CreateChildNodeFrom(target_parent, source_child, "ClonedChild");
        },
        "Original node no longer exists in its scene");
}

} // namespace
