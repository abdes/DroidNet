//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Detail/GraphData.h>
#include <Oxygen/Scene/Detail/NodeData.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::ObjectMetaData;
using oxygen::ResourceHandle;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;
using oxygen::scene::detail::NodeData;
using oxygen::scene::detail::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class TestSceneNodeImpl final : public SceneNodeImpl {
public:
    explicit TestSceneNodeImpl(const std::string& name = "TestNode", const Flags& flags = kDefaultFlags)
        : SceneNodeImpl(name, flags)
    {
    }
    using SceneNodeImpl::ClearTransformDirty;
};

//------------------------------------------------------------------------------
// Base Fixture for All SceneNodeImpl Tests
//------------------------------------------------------------------------------
class SceneNodeImplTestBase : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize test environment for each test case
    }

    // Helper: Create a node with default flags
    static auto CreateDefaultNode(const std::string& name = "TestNode")
    {
        return TestSceneNodeImpl { name };
    }

    // Helper: Verify all flag states match expected values
    static void ExpectFlagState(
        const SceneNodeImpl::Flags& flags, const SceneNodeFlags flag,
        const bool effective, const bool inherited, const bool dirty = false, const bool previous = false)
    {
        EXPECT_EQ(flags.GetEffectiveValue(flag), effective);
        EXPECT_EQ(flags.IsInherited(flag), inherited);
        EXPECT_EQ(flags.IsDirty(flag), dirty);
        EXPECT_EQ(flags.GetPreviousValue(flag), previous);
        EXPECT_FALSE(flags.GetPendingValue(flag)); // Should be false by default
    }
};

//------------------------------------------------------------------------------
// Default Flags Tests
//------------------------------------------------------------------------------
class SceneNodeImplDefaultFlagsTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, Construction_InitializesWithCorrectName)
{
    // Arrange: Create node with default flags
    const auto node = SceneNodeImpl("TestNode");

    // Act/Assert: Node should have correct name
    EXPECT_EQ(node.GetName(), "TestNode");
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, DefaultFlags_VisibleSetCorrectly)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    const auto& flags = node.GetFlags();

    // Act/Assert: Visible flag should be true and not inherited
    ExpectFlagState(flags, SceneNodeFlags::kVisible, true, false);
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, DefaultFlags_StaticSetCorrectly)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    const auto& flags = node.GetFlags();

    // Act/Assert: Static flag should be false and not inherited
    ExpectFlagState(flags, SceneNodeFlags::kStatic, false, false);
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, DefaultFlags_InheritedFlagsSetCorrectly)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    const auto& flags = node.GetFlags();

    // Act/Assert: Shadow-related flags should be false but inherited
    ExpectFlagState(flags, SceneNodeFlags::kCastsShadows, false, true);
    ExpectFlagState(flags, SceneNodeFlags::kReceivesShadows, false, true);
    ExpectFlagState(flags, SceneNodeFlags::kRayCastingSelectable, false, true);
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, DefaultFlags_IgnoreParentTransformSetCorrectly)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    const auto& flags = node.GetFlags();

    // Act/Assert: IgnoreParentTransform should be false and not inherited
    ExpectFlagState(flags, SceneNodeFlags::kIgnoreParentTransform, false, false);
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, DefaultFlags_AllPendingAndDirtyBitsFalse)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    const auto& flags = node.GetFlags();

    // Act/Assert: All pending, dirty, and previous bits should be false
    for (int i = 0; i < static_cast<int>(SceneNodeFlags::kCount); ++i) {
        const auto flag = static_cast<SceneNodeFlags>(i);
        EXPECT_FALSE(flags.GetPendingValue(flag)) << "Pending bit should be false for flag " << i;
        EXPECT_FALSE(flags.IsDirty(flag)) << "Dirty bit should be false for flag " << i;
        EXPECT_FALSE(flags.GetPreviousValue(flag)) << "Previous bit should be false for flag " << i;
    }
}

NOLINT_TEST_F(SceneNodeImplDefaultFlagsTest, ConstructorWithCustomFlags_PreservesCustomValues)
{
    // Arrange: Create custom flags with specific values
    const auto custom_flags
        = SceneNodeImpl::Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
              .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

    // Act: Construct node with custom flags
    auto node = SceneNodeImpl("CustomNode", custom_flags);

    // Assert: Custom flag values should be preserved
    const auto& flags = node.GetFlags();
    EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flags.GetEffectiveValue(SceneNodeFlags::kStatic));
}

//------------------------------------------------------------------------------
// Name Management Tests
//------------------------------------------------------------------------------
class SceneNodeImplNameTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplNameTest, SetName_UpdatesNameCorrectly)
{
    // Arrange: Create node with initial name
    auto node = CreateDefaultNode("InitialName");
    EXPECT_EQ(node.GetName(), "InitialName");

    // Act: Change node name
    node.SetName("NewName");

    // Assert: Name should be updated
    EXPECT_EQ(node.GetName(), "NewName");
}

NOLINT_TEST_F(SceneNodeImplNameTest, SetName_HandlesEmptyName)
{
    // Arrange: Create node with initial name
    auto node = CreateDefaultNode("InitialName");

    // Act: Set empty name
    node.SetName("");

    // Assert: Empty name should be accepted
    EXPECT_EQ(node.GetName(), "");
}

//------------------------------------------------------------------------------
// Flag Manipulation Tests
//------------------------------------------------------------------------------
class SceneNodeImplFlagManipulationTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplFlagManipulationTest, SetInherited_MakesFlagDirty)
{
    // Arrange: Create node with default flags
    auto node = CreateDefaultNode();
    auto& flags = node.GetFlags();

    // Act: Set visible flag to inherit from parent
    flags.SetInherited(SceneNodeFlags::kVisible, true);

    // Assert: Flag should be dirty and marked as inherited
    EXPECT_TRUE(flags.IsDirty(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flags.IsInherited(SceneNodeFlags::kVisible));
}

NOLINT_TEST_F(SceneNodeImplFlagManipulationTest, SetLocalValue_MakesFlagDirtyAndDisablesInheritance)
{
    // Arrange: Create node and set flag to inherit
    auto node = CreateDefaultNode();
    auto& flags = node.GetFlags();
    flags.SetInherited(SceneNodeFlags::kStatic, true);

    // Act: Set local value for the flag
    flags.SetLocalValue(SceneNodeFlags::kStatic, true);

    // Assert: Flag should be dirty and inheritance should be disabled
    EXPECT_TRUE(flags.IsDirty(SceneNodeFlags::kStatic));
    EXPECT_FALSE(flags.IsInherited(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneNodeImplFlagManipulationTest, ProcessDirtyFlag_ClearsDirtyState)
{
    // Arrange: Create node and make a flag dirty
    auto node = CreateDefaultNode();
    auto& flags = node.GetFlags();
    flags.SetLocalValue(SceneNodeFlags::kVisible, false);
    EXPECT_TRUE(flags.IsDirty(SceneNodeFlags::kVisible));

    // Act: Process the dirty flag
    const auto result = flags.ProcessDirtyFlag(SceneNodeFlags::kVisible);

    // Assert: Flag should no longer be dirty and processing should succeed
    EXPECT_TRUE(result);
    EXPECT_FALSE(flags.IsDirty(SceneNodeFlags::kVisible));
    EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Transform Dirty Flag Management Tests
//------------------------------------------------------------------------------
class SceneNodeImplTransformDirtyTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplTransformDirtyTest, NewNode_StartsWithDirtyTransform)
{
    // Arrange: Create a new node

    // Act: Check initial transform dirty state
    const auto node = CreateDefaultNode();

    // Assert: New nodes should start with dirty transform
    EXPECT_TRUE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformDirtyTest, ClearTransformDirty_ClearsFlag)
{
    // Arrange: Create node with dirty transform
    auto node = CreateDefaultNode();
    EXPECT_TRUE(node.IsTransformDirty());

    // Act: Clear transform dirty flag
    node.ClearTransformDirty();

    // Assert: Transform should no longer be dirty
    EXPECT_FALSE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformDirtyTest, MarkTransformDirty_SetsFlag)
{
    // Arrange: Create node and clear transform dirty
    auto node = CreateDefaultNode();
    node.ClearTransformDirty();
    EXPECT_FALSE(node.IsTransformDirty());

    // Act: Mark transform as dirty
    node.MarkTransformDirty();

    // Assert: Transform should be dirty
    EXPECT_TRUE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformDirtyTest, MarkTransformDirty_MultipleCallsRemainDirty)
{
    // Arrange: Create node and clear transform dirty
    auto node = CreateDefaultNode();
    node.ClearTransformDirty();

    // Act: Mark transform dirty multiple times
    node.MarkTransformDirty();
    node.MarkTransformDirty();
    node.MarkTransformDirty();

    // Assert: Transform should remain dirty
    EXPECT_TRUE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformDirtyTest, TransformDirtyLifecycle_CompleteWorkflow)
{
    // Arrange: Create node (starts dirty)
    auto node = CreateDefaultNode();
    EXPECT_TRUE(node.IsTransformDirty());

    // Act: Clear dirty flag
    node.ClearTransformDirty();

    // Assert: Should be clean
    EXPECT_FALSE(node.IsTransformDirty());

    // Act: Mark dirty again
    node.MarkTransformDirty();

    // Assert: Should be dirty
    EXPECT_TRUE(node.IsTransformDirty());

    // Act: Clear again
    node.ClearTransformDirty();

    // Assert: Should be clean again
    EXPECT_FALSE(node.IsTransformDirty());
}

//------------------------------------------------------------------------------
// Hierarchy Management Tests
//------------------------------------------------------------------------------
class SceneNodeImplHierarchyTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplHierarchyTest, HierarchyHandles_SetAndGetParent)
{
    // Arrange: Create node and parent handle
    auto node = CreateDefaultNode();
    const auto parent_handle = ResourceHandle { 42 };

    // Act: Set parent handle
    auto& graph_node = node.AsGraphNode();
    graph_node.SetParent(parent_handle);

    // Assert: Parent handle should be stored correctly
    EXPECT_EQ(graph_node.GetParent(), parent_handle);
}

NOLINT_TEST_F(SceneNodeImplHierarchyTest, HierarchyHandles_SetAndGetFirstChild)
{
    // Arrange: Create node and child handle
    auto node = CreateDefaultNode();
    const auto child_handle = ResourceHandle { 43 };

    // Act: Set first child handle
    auto& graph_node = node.AsGraphNode();
    graph_node.SetFirstChild(child_handle);

    // Assert: First child handle should be stored correctly
    EXPECT_EQ(graph_node.GetFirstChild(), child_handle);
}

NOLINT_TEST_F(SceneNodeImplHierarchyTest, HierarchyHandles_SetAndGetSiblings)
{
    // Arrange: Create node and sibling handles
    auto node = CreateDefaultNode();
    const auto next_handle = ResourceHandle { 44 };
    const auto prev_handle = ResourceHandle { 45 };

    // Act: Set sibling handles
    auto& graph_node = node.AsGraphNode();
    graph_node.SetNextSibling(next_handle);
    graph_node.SetPrevSibling(prev_handle);

    // Assert: Sibling handles should be stored correctly
    EXPECT_EQ(graph_node.GetNextSibling(), next_handle);
    EXPECT_EQ(graph_node.GetPrevSibling(), prev_handle);
}

NOLINT_TEST_F(SceneNodeImplHierarchyTest, HierarchyHandles_InvalidHandlesAccepted)
{
    // Arrange: Create node and invalid handle
    auto node = CreateDefaultNode();
    const auto invalid_handle = ResourceHandle {};
    EXPECT_FALSE(invalid_handle.IsValid());

    // Act: Set all hierarchy relationships to invalid handles
    auto& graph_node = node.AsGraphNode();
    graph_node.SetParent(invalid_handle);
    graph_node.SetFirstChild(invalid_handle);
    graph_node.SetNextSibling(invalid_handle);
    graph_node.SetPrevSibling(invalid_handle);

    // Assert: Invalid handles should be stored correctly
    EXPECT_EQ(graph_node.GetParent(), invalid_handle);
    EXPECT_EQ(graph_node.GetFirstChild(), invalid_handle);
    EXPECT_EQ(graph_node.GetNextSibling(), invalid_handle);
    EXPECT_EQ(graph_node.GetPrevSibling(), invalid_handle);
}

NOLINT_TEST_F(SceneNodeImplHierarchyTest, HierarchyHandles_ConsistencyAcrossOperations)
{
    // Arrange: Create node and set all handles
    auto node = CreateDefaultNode();
    const auto parent_handle = ResourceHandle { 100 };
    const auto child_handle = ResourceHandle { 200 };
    const auto next_handle = ResourceHandle { 300 };
    const auto prev_handle = ResourceHandle { 400 };

    auto& graph_node = node.AsGraphNode();
    graph_node.SetParent(parent_handle);
    graph_node.SetFirstChild(child_handle);
    graph_node.SetNextSibling(next_handle);
    graph_node.SetPrevSibling(prev_handle);

    // Act: Verify all handles persist correctly

    // Assert: All handles should be preserved
    EXPECT_EQ(graph_node.GetParent(), parent_handle);
    EXPECT_EQ(graph_node.GetFirstChild(), child_handle);
    EXPECT_EQ(graph_node.GetNextSibling(), next_handle);
    EXPECT_EQ(graph_node.GetPrevSibling(), prev_handle);

    // Act: Update one handle and verify others unchanged
    const auto new_parent = ResourceHandle { 500 };
    graph_node.SetParent(new_parent);

    // Assert: Parent should be updated, others unchanged
    EXPECT_EQ(graph_node.GetParent(), new_parent);
    EXPECT_EQ(graph_node.GetFirstChild(), child_handle);
    EXPECT_EQ(graph_node.GetNextSibling(), next_handle);
    EXPECT_EQ(graph_node.GetPrevSibling(), prev_handle);
}

//------------------------------------------------------------------------------
// Cloning Tests
//------------------------------------------------------------------------------
class SceneNodeImplCloningTest : public SceneNodeImplTestBase { };

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_CreatesIndependentCopy)
{
    // Arrange: Create original node with custom properties
    const auto original = SceneNodeImpl("OriginalNode");

    // Arrange: Modify transform component
    auto& transform = original.GetComponent<TransformComponent>();
    transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f });
    transform.SetLocalRotation({ 0.707f, 0.707f, 0.0f, 0.0f });
    transform.SetLocalScale({ 2.0f, 2.0f, 2.0f });

    // Act: Clone the node
    const auto clone = original.Clone();

    // Assert: Clone should exist and be independent
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetName(), "OriginalNode");
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_PreservesTransformData)
{
    // Arrange: Create original with specific transform
    const auto original = SceneNodeImpl("OriginalNode");
    auto& transform = original.GetComponent<TransformComponent>();
    transform.SetLocalPosition({ 1.0f, 2.0f, 3.0f });
    transform.SetLocalScale({ 2.0f, 2.0f, 2.0f });

    // Act: Clone the node
    const auto clone = original.Clone();

    // Assert: Clone should preserve transform data
    ASSERT_NE(clone, nullptr);
    const auto& clone_transform = clone->GetComponent<TransformComponent>();
    EXPECT_EQ(clone_transform.GetLocalPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(clone_transform.GetLocalScale(), glm::vec3(2.0f, 2.0f, 2.0f));
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_ClonesAreIndependent)
{
    // Arrange: Create original node
    auto original = SceneNodeImpl("OriginalNode");
    const auto clone = original.Clone();
    ASSERT_NE(clone, nullptr);

    // Act: Modify original name
    original.SetName("ChangedOriginal");

    // Act: Modify clone name independently
    clone->SetName("ClonedNode");

    // Assert: Changes should be independent
    EXPECT_EQ(original.GetName(), "ChangedOriginal");
    EXPECT_EQ(clone->GetName(), "ClonedNode");
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_PreservesObjectMetaData)
{
    // Arrange: Create original node with a specific name
    const std::string original_name = "MetaDataNode";
    const auto original = SceneNodeImpl(original_name);

    // Act: Clone the node
    const auto clone = original.Clone();

    // Assert: Clone should preserve ObjectMetaData (name)
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetComponent<ObjectMetaData>().GetName(), original_name);
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_ObjectMetaDataIsIndependent)
{
    // Arrange: Create original node and clone it
    const auto original = SceneNodeImpl("OriginalMeta");
    const auto clone = original.Clone();
    ASSERT_NE(clone, nullptr);

    // Act: Change name in original and clone independently
    original.GetComponent<ObjectMetaData>().SetName("ChangedOriginal");
    clone->GetComponent<ObjectMetaData>().SetName("ChangedClone");

    // Assert: Names are independent
    EXPECT_EQ(original.GetComponent<ObjectMetaData>().GetName(), "ChangedOriginal");
    EXPECT_EQ(clone->GetComponent<ObjectMetaData>().GetName(), "ChangedClone");
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_PreservesNodeDataFlags)
{
    // Arrange: Create original node with custom flags
    SceneNodeImpl::Flags custom_flags;
    custom_flags.SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
    custom_flags.SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));
    const auto original = SceneNodeImpl("FlagNode", custom_flags);

    // Act: Clone the node
    const auto clone = original.Clone();

    // Assert: Clone should preserve NodeData flags
    ASSERT_NE(clone, nullptr);
    const auto& orig_flags = original.GetComponent<NodeData>().flags_;
    const auto& clone_flags = clone->GetComponent<NodeData>().flags_;
    EXPECT_EQ(clone_flags.GetEffectiveValue(SceneNodeFlags::kVisible), orig_flags.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_EQ(clone_flags.GetEffectiveValue(SceneNodeFlags::kStatic), orig_flags.GetEffectiveValue(SceneNodeFlags::kStatic));
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_NodeDataFlagsAreIndependent)
{
    // Arrange: Create original node and clone it
    const auto original = SceneNodeImpl("FlagIndependence");
    const auto clone = original.Clone();
    ASSERT_NE(clone, nullptr);

    // Act: Change a flag in original and clone independently
    original.GetComponent<NodeData>().flags_.SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false));
    clone->GetComponent<NodeData>().flags_.SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true));

    // Assert: Flags are independent
    EXPECT_FALSE(original.GetComponent<NodeData>().flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(clone->GetComponent<NodeData>().flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
}

NOLINT_TEST_F(SceneNodeImplCloningTest, Clone_PreservesGraphDataOrphansClone)
{
    // Arrange: Create original with hierarchy handles
    auto original = SceneNodeImpl("OriginalNode");
    auto& graph_node = original.AsGraphNode();
    graph_node.SetParent(ResourceHandle { 100 });
    graph_node.SetFirstChild(ResourceHandle { 200 });
    graph_node.SetNextSibling(ResourceHandle { 300 });
    graph_node.SetPrevSibling(ResourceHandle { 400 });

    // Act: Clone the node
    const auto clone = original.Clone();

    // Assert: Clone should have no hierarchy relationships (orphaned)
    ASSERT_NE(clone, nullptr);
    const auto& clone_graph = clone->AsGraphNode();
    EXPECT_FALSE(clone_graph.GetParent().IsValid());
    EXPECT_FALSE(clone_graph.GetFirstChild().IsValid());
    EXPECT_FALSE(clone_graph.GetNextSibling().IsValid());
    EXPECT_FALSE(clone_graph.GetPrevSibling().IsValid());
}

//------------------------------------------------------------------------------
// SceneNodeImplTransformTest: Transform System Integration
//------------------------------------------------------------------------------

class MockScene final : public oxygen::scene::Scene {
public:
    MockScene()
        : Scene("MockScene", 1024)
    {
    }

    // Helper: Add a SceneNodeImpl to the mock scene for testing
    auto AddNodeForTesting(const std::string& name) -> ResourceHandle
    {
        const auto node = CreateNode(name);
        return node.GetHandle();
    }
};

class SceneNodeImplTransformTest : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Create mock scene for transform tests
        mock_scene_ = std::make_shared<MockScene>();
    }

    void TearDown() override
    {
        // Clean up: Reset mock scene
        mock_scene_.reset();
    }

    std::shared_ptr<MockScene> mock_scene_;
};

NOLINT_TEST_F(SceneNodeImplTransformTest, UpdateTransforms_RootNodeSucceeds)
{
    // Arrange: Create root node in scene
    const auto node_handle = mock_scene_->AddNodeForTesting("RootNode");
    auto& node = mock_scene_->GetNodeImplRef(node_handle);

    // Arrange: Set as root (invalid parent handle) and ensure dirty
    node.AsGraphNode().SetParent(ResourceHandle {});
    node.MarkTransformDirty();
    EXPECT_TRUE(node.IsTransformDirty());

    // Act: Update transforms
    node.UpdateTransforms(*mock_scene_);

    // Assert: Transform should no longer be dirty
    EXPECT_FALSE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformTest, UpdateTransforms_WithParentSucceeds)
{
    // Arrange: Create parent and child nodes in scene
    const auto parent_handle = mock_scene_->AddNodeForTesting("Parent");
    const auto child_handle = mock_scene_->AddNodeForTesting("Child");

    auto& child_impl = mock_scene_->GetNodeImplRef(child_handle);
    auto& parent_impl = mock_scene_->GetNodeImplRef(parent_handle);

    // Arrange: Set up parent-child relationship
    child_impl.AsGraphNode().SetParent(parent_handle);
    child_impl.MarkTransformDirty();

    // Act: Update parent first (required for child update)
    parent_impl.UpdateTransforms(*mock_scene_);

    // Act: Update child transform
    child_impl.UpdateTransforms(*mock_scene_);

    // Assert: Child transform should be clean
    EXPECT_FALSE(child_impl.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformTest, UpdateTransforms_IgnoreParentTransformFlag)
{
    // Arrange: Create node and set to ignore parent transform
    const auto node_handle = mock_scene_->AddNodeForTesting("TestNode");
    auto& node = mock_scene_->GetNodeImplRef(node_handle);

    auto& flags = node.GetFlags();
    flags.SetLocalValue(SceneNodeFlags::kIgnoreParentTransform, true);
    flags.ProcessDirtyFlag(SceneNodeFlags::kIgnoreParentTransform);

    // Arrange: Create parent and set relationship
    const auto parent_handle = mock_scene_->AddNodeForTesting("Parent");
    node.AsGraphNode().SetParent(parent_handle);
    node.MarkTransformDirty();

    // Act: Update transforms (should succeed and ignore parent)
    node.UpdateTransforms(*mock_scene_);

    // Assert: Transform should be clean
    EXPECT_FALSE(node.IsTransformDirty());
}

NOLINT_TEST_F(SceneNodeImplTransformTest, UpdateTransforms_CleanTransformIsNoOp)
{
    // Arrange: Create node and clear transform dirty flag
    const auto node_handle = mock_scene_->AddNodeForTesting("TestNode");
    auto& node = mock_scene_->GetNodeImplRef(node_handle);
    node.UpdateTransforms(*mock_scene_);
    EXPECT_FALSE(node.IsTransformDirty());

    // Act: Update transforms on clean node
    node.UpdateTransforms(*mock_scene_);

    // Assert: Transform should remain clean (no-op)
    EXPECT_FALSE(node.IsTransformDirty());
}

} // namespace
