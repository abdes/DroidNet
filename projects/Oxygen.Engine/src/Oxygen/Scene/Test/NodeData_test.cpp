//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Detail/NodeData.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>

using namespace oxygen::scene;
using namespace oxygen::scene::detail;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class NodeDataTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Set up default and custom flags for testing
        default_flags_ = NodeData::Flags {}
                             .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
                             .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetEffectiveValueBit(false));

        custom_flags_ = NodeData::Flags {}
                            .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
                            .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetEffectiveValueBit(true))
                            .SetFlag(SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetEffectiveValueBit(true));
    }

    // Helper: Validate flags and transform state match expected values
    void ExpectNodeDataState(const NodeData& node_data, const NodeData::Flags& expected_flags, bool expected_transform_dirty)
    {
        EXPECT_EQ(node_data.flags_, expected_flags);
        EXPECT_EQ(node_data.transform_dirty_, expected_transform_dirty);
    }

    NodeData::Flags default_flags_;
    NodeData::Flags custom_flags_;
};

//------------------------------------------------------------------------------
// Construction Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, Constructor_InitializesWithDefaultFlags)
{
    // Arrange: Use pre-configured default flags from SetUp
    // (default_flags_ already configured with visible=true, castsShadows=false)

    // Act: Create NodeData with default flags
    auto node_data = NodeData(default_flags_);

    // Assert: Verify flags are set correctly and transform_dirty defaults to true
    EXPECT_EQ(node_data.flags_, default_flags_);
    EXPECT_TRUE(node_data.transform_dirty_);
}

NOLINT_TEST_F(NodeDataTest, Constructor_InitializesWithCustomFlags)
{
    // Arrange: Use pre-configured custom flags from SetUp
    // (custom_flags_ configured with visible=false, castsShadows=true, receivesShadows=true)

    // Act: Create NodeData with custom flags
    auto node_data = NodeData(custom_flags_);

    // Assert: Verify custom flags are set correctly and transform_dirty defaults to true
    EXPECT_EQ(node_data.flags_, custom_flags_);
    EXPECT_TRUE(node_data.transform_dirty_);
}

//------------------------------------------------------------------------------
// Copy Constructor Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, CopyConstructor_PreservesAllDataWhenTransformClean)
{
    // Arrange: Create original NodeData with custom flags and clean transform state
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;

    // Act: Create copy using copy constructor
    auto copy = NodeData(original);

    // Assert: Verify all data is copied correctly, including the clean transform state
    ExpectNodeDataState(copy, original.flags_, false);
    EXPECT_EQ(copy.transform_dirty_, original.transform_dirty_);
}

NOLINT_TEST_F(NodeDataTest, CopyConstructor_PreservesAllDataWhenTransformDirty)
{
    // Arrange: Create original NodeData with custom flags and dirty transform state
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = true;

    // Act: Create copy using copy constructor
    auto copy = NodeData(original);

    // Assert: Verify all data is copied correctly, including the dirty transform state
    ExpectNodeDataState(copy, original.flags_, true);
}

//------------------------------------------------------------------------------
// Copy Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, CopyAssignment_PreservesAllData)
{
    // Arrange: Create source with custom flags (clean transform) and target with different state
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;
    auto target = NodeData(default_flags_);
    target.transform_dirty_ = true;

    // Act: Assign original to target using copy assignment
    target = original;

    // Assert: Verify target now matches original's state completely
    ExpectNodeDataState(target, original.flags_, false);
}

//------------------------------------------------------------------------------
// Move Constructor Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, MoveConstructor_TransfersDataAndInvalidatesSource)
{
    // Arrange: Create original with custom flags and capture expected state before move
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;
    auto expected_flags = original.flags_;
    auto expected_transform_dirty = original.transform_dirty_;

    // Act: Move construct new object from original
    auto moved = NodeData(std::move(original));

    // Assert: Verify data transferred to moved object and original is in valid moved-from state
    ExpectNodeDataState(moved, expected_flags, expected_transform_dirty);
    EXPECT_FALSE(original.flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_FALSE(original.transform_dirty_);
}

//------------------------------------------------------------------------------
// Move Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, MoveAssignment_TransfersDataAndInvalidatesSource)
{
    // Arrange: Create source and target, capture source state before move
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;
    auto expected_flags = original.flags_;
    auto expected_transform_dirty = original.transform_dirty_;
    auto target = NodeData(default_flags_);

    // Act: Move assign original to target
    target = std::move(original);

    // Assert: Verify data transferred to target and original is in valid moved-from state
    ExpectNodeDataState(target, expected_flags, expected_transform_dirty);
    EXPECT_FALSE(original.flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_FALSE(original.transform_dirty_);
}

//------------------------------------------------------------------------------
// Self-Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, SelfMoveAssignment_HandledCorrectly)
{
    // Arrange: Create NodeData and capture state before self-assignment
    auto node_data = NodeData(custom_flags_);
    node_data.transform_dirty_ = false;
    auto expected_flags = node_data.flags_;
    auto expected_transform_dirty = node_data.transform_dirty_;

    // Act: Perform self move-assignment (edge case that should be handled gracefully)
    node_data = std::move(node_data);

    // Assert: Verify object remains in valid, unchanged state after self-assignment
    ExpectNodeDataState(node_data, expected_flags, expected_transform_dirty);
}

//------------------------------------------------------------------------------
// Cloning Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, IsCloneable_ReturnsTrue)
{
    // Arrange: Create NodeData instance
    auto node_data = NodeData(default_flags_);

    // Act & Assert: Verify NodeData reports itself as cloneable
    EXPECT_TRUE(node_data.IsCloneable());
}

NOLINT_TEST_F(NodeDataTest, Clone_PreservesFlags)
{
    // Arrange: Create original NodeData with custom flags
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;

    // Act: Clone the original NodeData
    auto cloned = original.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify clone preserves the original's flags
    ASSERT_NE(cloned_node_data, nullptr);
    EXPECT_EQ(cloned_node_data->flags_, original.flags_);
}

NOLINT_TEST_F(NodeDataTest, Clone_PreservesTransformDirtyStateWhenFalse)
{
    // Arrange: Create original with clean transform state
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;

    // Act: Clone the original NodeData
    auto cloned = original.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify clone preserves both flags and clean transform state
    ASSERT_NE(cloned_node_data, nullptr);
    ExpectNodeDataState(*cloned_node_data, original.flags_, false);
}

NOLINT_TEST_F(NodeDataTest, Clone_PreservesTransformDirtyStateWhenTrue)
{
    // Arrange: Create original with dirty transform state
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = true;

    // Act: Clone the original NodeData
    auto cloned = original.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify clone preserves both flags and dirty transform state
    ASSERT_NE(cloned_node_data, nullptr);
    ExpectNodeDataState(*cloned_node_data, original.flags_, true);
}

NOLINT_TEST_F(NodeDataTest, Clone_CreatesIndependentCopy)
{
    // Arrange: Create original and clone it
    auto original = NodeData(custom_flags_);
    original.transform_dirty_ = false;
    auto cloned = original.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Act: Modify original after cloning to test independence
    original.flags_.SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true));
    original.transform_dirty_ = true;

    // Assert: Verify clone remains unchanged despite original modifications
    EXPECT_FALSE(cloned_node_data->flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
    EXPECT_FALSE(cloned_node_data->transform_dirty_);
}

//------------------------------------------------------------------------------
// Complex Flag Configuration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, ComplexFlagConfiguration_CopyAndCloneWork)
{
    // Arrange: Create NodeData with complex flag configuration
    auto complex_flags = NodeData::Flags {}
                             .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(false))
                             .SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetEffectiveValueBit(true))
                             .SetFlag(SceneNodeFlags::kReceivesShadows, SceneFlag {}.SetEffectiveValueBit(true));
    auto node_data = NodeData(complex_flags);
    node_data.transform_dirty_ = false;

    // Act: Test both copy constructor and cloning with complex flags
    auto copy = NodeData(node_data);
    auto cloned = node_data.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify both copy and clone preserve complex flag configuration
    ExpectNodeDataState(copy, complex_flags, false);
    ExpectNodeDataState(*cloned_node_data, complex_flags, false);
}

//------------------------------------------------------------------------------
// Edge Case Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, AllFlagsDefaultConfiguration_CloneWorks)
{
    // Arrange: Create NodeData with all default flags and clean transform
    auto all_default = NodeData::Flags {};
    auto node_data = NodeData(all_default);
    node_data.transform_dirty_ = false;

    // Act: Clone NodeData with default configuration
    auto cloned = node_data.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify clone preserves default flag state and clean transform
    ExpectNodeDataState(*cloned_node_data, all_default, false);
}

NOLINT_TEST_F(NodeDataTest, FlagModificationAfterConstruction_ClonePreservesModifications)
{
    // Arrange: Create NodeData and modify flags after construction
    auto node_data = NodeData(default_flags_);
    node_data.flags_.SetFlag(SceneNodeFlags::kCastsShadows, SceneFlag {}.SetEffectiveValueBit(true));
    node_data.transform_dirty_ = false;

    // Act: Clone NodeData with post-construction modifications
    auto cloned = node_data.Clone();
    auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

    // Assert: Verify clone preserves the modified flag state
    EXPECT_TRUE(cloned_node_data->flags_.GetEffectiveValue(SceneNodeFlags::kCastsShadows));
    EXPECT_FALSE(cloned_node_data->transform_dirty_);
}

} // namespace
