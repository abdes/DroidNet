//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h> // For mocking scene in update tests
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::ObjectMetaData;
using oxygen::ResourceHandle;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlags;
using oxygen::scene::SceneNodeData;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::TransformComponent;

namespace {

class SceneNodeImplTest : public ::testing::Test {
protected:
    SceneNodeImplTest() = default;
    // Helper to create a node with default flags
    static SceneNodeImpl CreateDefaultNode(const std::string& name = "TestNode")
    {
        return SceneNodeImpl(name);
    }
};

// -----------------------------------------------------------------------------
// Constructor and Component Integration Tests
// -----------------------------------------------------------------------------

//! Tests that SceneNodeImpl constructor initializes all flags to their correct
//! default values.
TEST_F(SceneNodeImplTest, DefaultFlagsAreSetCorrectly)
{
    const SceneNodeImpl node("TestNode");
    const auto& data = node.GetComponent<SceneNodeData>();
    const auto& flags = data.GetFlags();
    for (int i = 0; i < static_cast<int>(SceneNodeFlags::kCount); ++i) {
        auto flag = static_cast<SceneNodeFlags>(i);
        switch (flag) {
        case SceneNodeFlags::kVisible:
            EXPECT_TRUE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kStatic:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kCastsShadows:
        case SceneNodeFlags::kReceivesShadows:
        case SceneNodeFlags::kRayCastingSelectable:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_TRUE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kIgnoreParentTransform:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        default:
            break;
        }
        // All other bits should be false by default
        EXPECT_FALSE(flags.GetPendingValue(flag)) << "Pending bit should be false for flag " << i;
        EXPECT_FALSE(flags.IsDirty(flag)) << "Dirty bit should be false for flag " << i;
        EXPECT_FALSE(flags.GetPreviousValue(flag)) << "Previous bit should be false for flag " << i;
    }
}

//! Tests that SceneNodeImpl constructor accepts custom flag configurations.
TEST_F(SceneNodeImplTest, ConstructorWithCustomFlags)
{
    // Test construction with custom flags instead of defaults
    auto custom_flags
        = SceneNodeImpl::Flags {}
              .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
              .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(true));

    SceneNodeImpl node("CustomNode", custom_flags);

    const auto& flags = node.GetFlags();
    EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flags.GetEffectiveValue(SceneNodeFlags::kStatic));
}

//! Tests that all required components are properly accessible and consistent.
TEST_F(SceneNodeImplTest, ComponentAccessAndIntegrity)
{
    SceneNodeImpl node = CreateDefaultNode("TestNode");

    // Test component access
    EXPECT_NO_THROW([[maybe_unused]] auto& _ = node.GetComponent<SceneNodeData>());
    EXPECT_NO_THROW([[maybe_unused]] auto& _ = node.GetComponent<ObjectMetaData>());
    EXPECT_NO_THROW([[maybe_unused]] auto& _ = node.GetComponent<TransformComponent>());

    // Test component consistency
    EXPECT_EQ(node.GetName(), "TestNode");
    EXPECT_EQ(&node.GetFlags(), &node.GetComponent<SceneNodeData>().GetFlags());
}

//! Tests that node names can be set during construction and modified later.
TEST_F(SceneNodeImplTest, NameIsStoredAndMutable)
{
    SceneNodeImpl node("TestNode");
    EXPECT_EQ(node.GetName(), "TestNode");
    node.SetName("Renamed");
    EXPECT_EQ(node.GetName(), "Renamed");
}

// -----------------------------------------------------------------------------
// Flag Processing and Inheritance Tests
// -----------------------------------------------------------------------------

//! Tests that setting a flag to inherited state properly marks it as dirty.
//! Sets a flag to inherit from parent and verify dirty state tracking.
TEST_F(SceneNodeImplTest, FlagInheritanceBehavior)
{
    SceneNodeImpl node = CreateDefaultNode();
    auto& flags = node.GetFlags();

    // Test inherited flag becomes dirty when set to inherit
    flags.SetInherited(SceneNodeFlags::kVisible, true);
    EXPECT_TRUE(flags.IsDirty(SceneNodeFlags::kVisible));
    EXPECT_TRUE(flags.IsInherited(SceneNodeFlags::kVisible));
}

//! Tests the complete flag processing workflow from dirty to clean state. Sets
//! a local flag value, process it, and verify the dirty flag is cleared.
TEST_F(SceneNodeImplTest, FlagProcessingIntegration)
{
    SceneNodeImpl node = CreateDefaultNode();
    auto& flags = node.GetFlags();

    // Set a local value and process
    flags.SetLocalValue(SceneNodeFlags::kVisible, false);
    EXPECT_TRUE(flags.IsDirty(SceneNodeFlags::kVisible));

    flags.ProcessDirtyFlag(SceneNodeFlags::kVisible);
    EXPECT_FALSE(flags.IsDirty(SceneNodeFlags::kVisible));
    EXPECT_FALSE(flags.GetEffectiveValue(SceneNodeFlags::kVisible));
}

// -----------------------------------------------------------------------------
// Transform Dirty Flag Lifecycle Tests
// -----------------------------------------------------------------------------

//! Tests the complete lifecycle of transform dirty flag management. Verify
//! initial dirty state, clearing, marking dirty, and multiple operations.
TEST_F(SceneNodeImplTest, TransformDirtyFlagLifecycle)
{
    SceneNodeImpl node = CreateDefaultNode();

    // Node should start dirty (new nodes need initial transform update)
    EXPECT_TRUE(node.IsTransformDirty());

    // Clear and verify
    node.ClearTransformDirty();
    EXPECT_FALSE(node.IsTransformDirty());

    // Mark dirty multiple times
    node.MarkTransformDirty();
    EXPECT_TRUE(node.IsTransformDirty());
    node.MarkTransformDirty(); // Should remain dirty
    EXPECT_TRUE(node.IsTransformDirty());

    // Clear again
    node.ClearTransformDirty();
    EXPECT_FALSE(node.IsTransformDirty());
}

// -----------------------------------------------------------------------------
// Integration with SceneFlags Range Adapters
// -----------------------------------------------------------------------------

//! Tests integration with SceneFlags range adapters for iterating dirty flags.
//! Sets multiple flags dirty and verify they can be enumerated correctly.
TEST_F(SceneNodeImplTest, FlagRangeAdapterIntegration)
{
    SceneNodeImpl node = CreateDefaultNode();
    auto& flags = node.GetFlags();

    // Set some flags dirty
    flags.SetLocalValue(SceneNodeFlags::kVisible, false);
    flags.SetLocalValue(SceneNodeFlags::kStatic, true);

    // Test dirty flags range
    std::vector<SceneNodeFlags> dirty_flags;
    for (auto flag : flags.dirty_flags()) {
        dirty_flags.push_back(flag);
    }
    EXPECT_EQ(dirty_flags.size(), 2);
    EXPECT_THAT(dirty_flags,
        testing::UnorderedElementsAre(
            SceneNodeFlags::kVisible,
            SceneNodeFlags::kStatic));
}

// -----------------------------------------------------------------------------
// Hierarchy Management Edge Cases
// -----------------------------------------------------------------------------

//! Tests that hierarchy handle accessors and mutators work correctly. Set
//! parent, child, and sibling handles and verify they are stored properly.
TEST_F(SceneNodeImplTest, HierarchyAccessorsAndMutators)
{
    SceneNodeImpl node = CreateDefaultNode();
    oxygen::ResourceHandle parent { 42 };
    oxygen::ResourceHandle child { 43 };
    oxygen::ResourceHandle next { 44 };
    oxygen::ResourceHandle prev { 45 };

    node.SetParent(parent);
    node.SetFirstChild(child);
    node.SetNextSibling(next);
    node.SetPrevSibling(prev);

    EXPECT_EQ(node.GetParent(), parent);
    EXPECT_EQ(node.GetFirstChild(), child);
    EXPECT_EQ(node.GetNextSibling(), next);
    EXPECT_EQ(node.GetPrevSibling(), prev);
}
//! Tests behavior with invalid resource handles in hierarchy management. Set
//! all hierarchy relationships to invalid handles and verify proper handling.
TEST_F(SceneNodeImplTest, HierarchyInvalidHandles)
{
    SceneNodeImpl node = CreateDefaultNode();

    // Test with invalid handles
    ResourceHandle invalid_handle {};
    EXPECT_FALSE(invalid_handle.IsValid());

    node.SetParent(invalid_handle);
    node.SetFirstChild(invalid_handle);
    node.SetNextSibling(invalid_handle);
    node.SetPrevSibling(invalid_handle);

    EXPECT_EQ(node.GetParent(), invalid_handle);
    EXPECT_EQ(node.GetFirstChild(), invalid_handle);
    EXPECT_EQ(node.GetNextSibling(), invalid_handle);
    EXPECT_EQ(node.GetPrevSibling(), invalid_handle);
}

//! Tests that hierarchy handles maintain consistency across multiple
//! operations. Set all handles, verify persistence, then update one and verify
//! others unchanged
TEST_F(SceneNodeImplTest, HierarchyHandleConsistency)
{
    SceneNodeImpl node = CreateDefaultNode();

    ResourceHandle parent { 100 };
    ResourceHandle child { 200 };
    ResourceHandle next { 300 };
    ResourceHandle prev { 400 };

    // Set all handles
    node.SetParent(parent);
    node.SetFirstChild(child);
    node.SetNextSibling(next);
    node.SetPrevSibling(prev);

    // Verify handles persist correctly
    EXPECT_EQ(node.GetParent(), parent);
    EXPECT_EQ(node.GetFirstChild(), child);
    EXPECT_EQ(node.GetNextSibling(), next);
    EXPECT_EQ(node.GetPrevSibling(), prev);

    // Test handle updates
    ResourceHandle new_parent { 500 };
    node.SetParent(new_parent);
    EXPECT_EQ(node.GetParent(), new_parent);
    EXPECT_EQ(node.GetFirstChild(), child); // Other handles unchanged
}

// -----------------------------------------------------------------------------
// Transform System Integration Tests
// -----------------------------------------------------------------------------

// Mock Scene for UpdateTransforms - functional Scene subclass for testing.
class MockScene : public oxygen::scene::Scene {
public:
    MockScene()
        : oxygen::scene::Scene("MockScene", 1024)
    {
    }

    // Helper method to add a SceneNodeImpl to the mock scene for testing
    // Returns the handle that can be used to reference the node
    auto AddNodeForTesting(const std::string& name) -> ResourceHandle
    {
        auto node = CreateNode(name);
        return node.GetHandle();
    }
    // Helper method to add a SceneNodeImpl with custom data for testing
    auto AddNodeForTesting(const oxygen::scene::SceneNodeImpl& nodeImpl) -> ResourceHandle
    {
        // Create a node with the same name and copy the data
        std::string name { nodeImpl.GetName() }; // Convert string_view to string
        auto node = CreateNode(name);
        auto handle = node.GetHandle();

        // Get the node implementation and copy the data we need for testing
        auto& impl = GetNodeImplRef(handle);
        impl.SetParent(nodeImpl.GetParent());
        impl.SetFirstChild(nodeImpl.GetFirstChild());
        impl.SetNextSibling(nodeImpl.GetNextSibling());
        impl.SetPrevSibling(nodeImpl.GetPrevSibling());

        if (nodeImpl.IsTransformDirty()) {
            impl.MarkTransformDirty();
        } else {
            impl.ClearTransformDirty();
        }

        return handle;
    }
};

//! Tests transform updates in a parent-child hierarchy relationship. Create
//! parent and child nodes, update parent first, then child should inherit.
//! correctly
TEST_F(SceneNodeImplTest, UpdateTransformsWithParent)
{
    auto mock_scene = std::make_shared<MockScene>();

    // Create parent and child nodes in the scene
    auto parent_handle = mock_scene->AddNodeForTesting("Parent");
    auto child_handle = mock_scene->AddNodeForTesting("Child");

    // Set up parent-child relationship
    auto& child_impl = mock_scene->GetNodeImplRef(child_handle);
    auto& parent_impl = mock_scene->GetNodeImplRef(parent_handle);
    child_impl.SetParent(parent_handle);
    child_impl.MarkTransformDirty();

    // First update the parent's transform (parent should be a root node)
    parent_impl.UpdateTransforms(*mock_scene);

    // Now update the child's transforms - should succeed with the parent's transform available
    child_impl.UpdateTransforms(*mock_scene);
    EXPECT_FALSE(child_impl.IsTransformDirty());
}

//! Tests transform updates when the IgnoreParentTransform flag is set.
//! Scenario: Set a node to ignore parent transform and verify it updates
//! independently.
TEST_F(SceneNodeImplTest, UpdateTransformsIgnoreParentTransform)
{
    auto mock_scene = std::make_shared<MockScene>();
    auto node_handle = mock_scene->AddNodeForTesting("TestNode");
    auto& node = mock_scene->GetNodeImplRef(node_handle);

    auto& flags = node.GetFlags();

    // Set ignore parent transform flag
    flags.SetLocalValue(SceneNodeFlags::kIgnoreParentTransform, true);
    flags.ProcessDirtyFlag(SceneNodeFlags::kIgnoreParentTransform);

    // Create a parent node and set the relationship
    auto parent_handle = mock_scene->AddNodeForTesting("Parent");
    node.SetParent(parent_handle);
    node.MarkTransformDirty();

    // Update transforms - should succeed and ignore the parent transform
    node.UpdateTransforms(*mock_scene);
    EXPECT_FALSE(node.IsTransformDirty());
}

//! Tests that transform updates are no-op when the transform is already clean.
//! Clear transform dirty flag and verify UpdateTransforms does nothing.
TEST_F(SceneNodeImplTest, UpdateTransformsWhenClean)
{
    auto mock_scene = std::make_shared<MockScene>();
    auto node_handle = mock_scene->AddNodeForTesting("TestNode");
    auto& node = mock_scene->GetNodeImplRef(node_handle);

    // Node starts dirty but we clear it
    node.ClearTransformDirty();
    EXPECT_FALSE(node.IsTransformDirty());

    // Should be no-op when transform is already clean
    node.UpdateTransforms(*mock_scene);
    EXPECT_FALSE(node.IsTransformDirty());
}

//! Tests transform updates for root nodes (nodes without parents). Create a
//! root node and verify it updates its transform independently.
TEST_F(SceneNodeImplTest, UpdateTransformsAsRoot)
{
    auto mock_scene = std::make_shared<MockScene>();
    auto node_handle = mock_scene->AddNodeForTesting("RootNode");
    auto& node = mock_scene->GetNodeImplRef(node_handle);

    // Set as root (invalid parent handle)
    node.SetParent(oxygen::ResourceHandle {}); // Invalid handle = root

    node.MarkTransformDirty();
    node.UpdateTransforms(*mock_scene);
    EXPECT_FALSE(node.IsTransformDirty());
}

} // namespace
