//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::ObjectMetaData;
using oxygen::ResourceHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneCloningNodesTest : public ::testing::Test {
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
    void ExpectNodeValidWithName(const SceneNode& node, const std::string& expected_name)
    {
        EXPECT_TRUE(node.IsValid());
        auto impl_opt = node.GetObject();
        ASSERT_TRUE(impl_opt.has_value());
        EXPECT_EQ(impl_opt->get().GetName(), expected_name);
    }

    // Helper: Set transform values for testing component preservation
    void SetTransformValues(SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
    {
        auto impl_opt = node.GetObject();
        ASSERT_TRUE(impl_opt.has_value());
        auto& transform = impl_opt->get().GetComponent<TransformComponent>();
        transform.SetLocalPosition(position);
        transform.SetLocalScale(scale);
    }

    // Helper: Verify transform values match expected values
    void ExpectTransformValues(const SceneNode& node, const glm::vec3& expected_position, const glm::vec3& expected_scale)
    {
        auto impl_opt = node.GetObject();
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

NOLINT_TEST_F(SceneCloningNodesTest, BasicCloning_CreatesValidCloneWithNewName)
{
    // Arrange: Create original node in source scene
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    // Act: Clone node to target scene with new name
    auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");

    // Assert: Verify cloned node is valid and has correct name
    ExpectNodeValidWithName(cloned, "ClonedNode");

    // Assert: Verify original node is unchanged
    ExpectNodeValidWithName(original, "OriginalNode");
}

NOLINT_TEST_F(SceneCloningNodesTest, SameSceneCloning_CreatesIndependentNodes)
{
    // Arrange: Create original node in source scene
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    // Act: Clone node within the same scene
    auto cloned = source_scene_->CreateNodeFrom(original, "ClonedNode");

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

NOLINT_TEST_F(SceneCloningNodesTest, ComponentDataPreservation_TransformAndFlagsPreserved)
{
    // Arrange: Create original node and modify its components
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    auto original_impl_opt = original.GetObject();
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

    auto cloned_impl_opt = cloned.GetObject();
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

NOLINT_TEST_F(SceneCloningNodesTest, ObjectMetaDataPreservation_MetaDataComponentExists)
{
    // Arrange: Create original node (ObjectMetaData should exist by default)
    auto original = source_scene_->CreateNode("OriginalNode");
    ASSERT_TRUE(original.IsValid());

    auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    auto& original_impl = original_impl_opt->get();

    // Arrange: Verify ObjectMetaData exists on original
    EXPECT_TRUE(original_impl.HasComponent<ObjectMetaData>());

    // Act: Clone the node
    auto cloned = target_scene_->CreateNodeFrom(original, "ClonedNode");
    ASSERT_TRUE(cloned.IsValid());

    auto cloned_impl_opt = cloned.GetObject();
    ASSERT_TRUE(cloned_impl_opt.has_value());
    auto& cloned_impl = cloned_impl_opt->get();

    // Assert: Verify ObjectMetaData is preserved in clone
    EXPECT_TRUE(cloned_impl.HasComponent<ObjectMetaData>());
}

//------------------------------------------------------------------------------
// Independence Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneCloningNodesTest, ClonesAreIndependent_ModificationsDoNotAffectEachOther)
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
    auto original_impl_opt = original.GetObject();
    ASSERT_TRUE(original_impl_opt.has_value());
    auto& original_impl = original_impl_opt->get();
    original_impl.SetName("ModifiedOriginal");
    SetTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });

    // Assert: Verify clone is unaffected by original modifications
    ExpectNodeValidWithName(cloned, "ClonedNode");
    ExpectTransformValues(cloned, { 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });

    // Act: Modify clone node (name and transform)
    auto cloned_impl_opt = cloned.GetObject();
    ASSERT_TRUE(cloned_impl_opt.has_value());
    auto& cloned_impl = cloned_impl_opt->get();
    cloned_impl.SetName("ModifiedClone");
    SetTransformValues(cloned, { 100.0f, 200.0f, 300.0f }, { 3.0f, 3.0f, 3.0f });

    // Assert: Verify original is unaffected by clone modifications
    ExpectNodeValidWithName(original, "ModifiedOriginal");
    ExpectTransformValues(original, { 10.0f, 20.0f, 30.0f }, { 2.0f, 2.0f, 2.0f });
}

NOLINT_TEST_F(SceneCloningNodesTest, ClonesAreOrphaned_NoHierarchyRelationshipsPreserved)
{
    // Arrange: Create a parent-child hierarchy in source scene
    auto parent = source_scene_->CreateNode("Parent");
    auto child1_opt = source_scene_->CreateChildNode(parent, "Child1");
    auto child2_opt = source_scene_->CreateChildNode(parent, "Child2");

    ASSERT_TRUE(child1_opt.has_value());
    ASSERT_TRUE(child2_opt.has_value());
    auto child1 = child1_opt.value();
    auto child2 = child2_opt.value();

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

} // namespace
