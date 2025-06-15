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

class NodeDataTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Arrange: Set up default and custom flags for testing
    default_flags_ = NodeData::Flags {}
                       .SetFlag(SceneNodeFlags::kVisible,
                         SceneFlag {}.SetEffectiveValueBit(true))
                       .SetFlag(SceneNodeFlags::kCastsShadows,
                         SceneFlag {}.SetEffectiveValueBit(false));

    custom_flags_ = NodeData::Flags {}
                      .SetFlag(SceneNodeFlags::kVisible,
                        SceneFlag {}.SetEffectiveValueBit(false))
                      .SetFlag(SceneNodeFlags::kCastsShadows,
                        SceneFlag {}.SetEffectiveValueBit(true))
                      .SetFlag(SceneNodeFlags::kReceivesShadows,
                        SceneFlag {}.SetEffectiveValueBit(true));
  }

  // Helper: Validate flags and transform state match expected values
  static void ExpectNodeDataState(
    const NodeData& node_data, const NodeData::Flags& expected_flags)
  {
    EXPECT_EQ(node_data.flags_, expected_flags);
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
  const auto node_data = NodeData(default_flags_);

  // Assert: Verify flags are set correctly and transform_dirty defaults to
  // true
  EXPECT_EQ(node_data.flags_, default_flags_);
}

NOLINT_TEST_F(NodeDataTest, Constructor_InitializesWithCustomFlags)
{
  // Arrange: Use pre-configured custom flags from SetUp
  // (custom_flags_ configured with visible=false, castsShadows=true,
  // receivesShadows=true)

  // Act: Create NodeData with custom flags
  const auto node_data = NodeData(custom_flags_);

  // Assert: Verify custom flags are set correctly and transform_dirty
  // defaults to true
  EXPECT_EQ(node_data.flags_, custom_flags_);
}

//------------------------------------------------------------------------------
// Copy Constructor Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, CopyConstructor_PreservesAllData)
{
  // Arrange: Create original NodeData with custom flags and clean transform
  // state
  auto original = NodeData(custom_flags_);

  // Act: Create copy using copy constructor
  const auto copy = NodeData(original);
  // Assert: Verify all data is copied correctly, including the clean
  // transform state
  GCHECK_F(ExpectNodeDataState(copy, original.flags_));
}

//------------------------------------------------------------------------------
// Copy Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, CopyAssignment_PreservesAllData)
{
  // Arrange: Create source with custom flags and target with different state
  const auto original = NodeData(custom_flags_);
  auto target = NodeData(default_flags_);

  // Act: Assign original to target using copy assignment
  target = original;
  // Assert: Verify target now matches original's state completely
  GCHECK_F(ExpectNodeDataState(target, original.flags_));
}

//------------------------------------------------------------------------------
// Move Constructor Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, MoveConstructor_TransfersDataAndInvalidatesSource)
{
  // Arrange: Create original with custom flags and capture expected state
  // before move
  auto original = NodeData(custom_flags_);
  const auto expected_flags = original.flags_;

  // Act: Move construct new object from original
  const auto moved = NodeData(std::move(original));
  // Assert: Verify data transferred to moved object and original is in valid
  // moved-from state
  GCHECK_F(ExpectNodeDataState(moved, expected_flags));
  // NOLINTNEXTLINE(bugprone-use-after-move) - testing the state of moved-from
  // object
  EXPECT_FALSE(original.flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Move Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, MoveAssignment_TransfersDataAndInvalidatesSource)
{
  // Arrange: Create source and target, capture source state before move
  auto original = NodeData(custom_flags_);
  const auto expected_flags = original.flags_;
  auto target = NodeData(default_flags_);

  // Act: Move assign original to target
  target = std::move(original);
  // Assert: Verify data transferred to target and original is in valid
  // moved-from state
  GCHECK_F(ExpectNodeDataState(target, expected_flags));
  // NOLINTNEXTLINE(bugprone-use-after-move) - testing the state of moved-from
  // object
  EXPECT_FALSE(original.flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Self-Assignment Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, SelfMoveAssignment_HandledCorrectly)
{
  // Arrange: Create NodeData and capture state before self-assignment
  auto node_data = NodeData(custom_flags_);
  const auto expected_flags = node_data.flags_;

  // Act: Perform self move-assignment (edge case that should be handled
  // gracefully)
  node_data = std::move(node_data); // NOLINT(clang-diagnostic-self-move)
  // Assert: Verify object remains in valid, unchanged state after
  // self-assignment
  GCHECK_F(ExpectNodeDataState(node_data, expected_flags));
}

//------------------------------------------------------------------------------
// Cloning Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, IsCloneable_ReturnsTrue)
{
  // Arrange: Create NodeData instance
  const auto node_data = NodeData(default_flags_);

  // Act & Assert: Verify NodeData reports itself as cloneable
  EXPECT_TRUE(node_data.IsCloneable());
}

NOLINT_TEST_F(NodeDataTest, Clone_PreservesFlags)
{
  // Arrange: Create original NodeData with custom flags
  const auto original = NodeData(custom_flags_);

  // Act: Clone the original NodeData
  const auto cloned = original.Clone();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

  // Assert: Verify clone preserves the original's flags
  ASSERT_NE(cloned_node_data, nullptr);
  EXPECT_EQ(cloned_node_data->flags_, original.flags_);
}

NOLINT_TEST_F(NodeDataTest, Clone_CreatesIndependentCopy)
{
  // Arrange: Create original and clone it
  auto original = NodeData(custom_flags_);
  const auto cloned = original.Clone();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

  // Act: Modify original after cloning to test independence
  original.flags_.SetFlag(
    SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true));

  // Assert: Verify clone remains unchanged despite original modifications
  EXPECT_FALSE(
    cloned_node_data->flags_.GetEffectiveValue(SceneNodeFlags::kVisible));
}

//------------------------------------------------------------------------------
// Complex Flag Configuration Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, ComplexFlagConfiguration_CopyAndCloneWork)
{
  // Arrange: Create NodeData with complex flag configuration
  const auto complex_flags = NodeData::Flags {}
                               .SetFlag(SceneNodeFlags::kVisible,
                                 SceneFlag {}.SetEffectiveValueBit(false))
                               .SetFlag(SceneNodeFlags::kCastsShadows,
                                 SceneFlag {}.SetEffectiveValueBit(true))
                               .SetFlag(SceneNodeFlags::kReceivesShadows,
                                 SceneFlag {}.SetEffectiveValueBit(true));
  auto node_data = NodeData(complex_flags);

  // Act: Test both copy constructor and cloning with complex flags
  const auto copy = NodeData(node_data);
  const auto cloned = node_data.Clone();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* cloned_node_data = static_cast<NodeData*>(cloned.get());
  // Assert: Verify both copy and clone preserve complex flag configuration
  GCHECK_F(ExpectNodeDataState(copy, complex_flags));
  GCHECK_F(ExpectNodeDataState(*cloned_node_data, complex_flags));
}

//------------------------------------------------------------------------------
// Edge Case Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(NodeDataTest, AllFlagsDefaultConfiguration_CloneWorks)
{
  // Arrange: Create NodeData with all default flags and clean transform
  const auto all_default = NodeData::Flags {};
  const auto node_data = NodeData(all_default);

  // Act: Clone NodeData with default configuration
  const auto cloned = node_data.Clone();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* cloned_node_data = static_cast<NodeData*>(cloned.get());
  // Assert: Verify clone preserves default flag state and clean transform
  GCHECK_F(ExpectNodeDataState(*cloned_node_data, all_default));
}

NOLINT_TEST_F(
  NodeDataTest, FlagModificationAfterConstruction_ClonePreservesModifications)
{
  // Arrange: Create NodeData and modify flags after construction
  auto node_data = NodeData(default_flags_);
  node_data.flags_.SetFlag(
    SceneNodeFlags::kCastsShadows, SceneFlag {}.SetEffectiveValueBit(true));

  // Act: Clone NodeData with post-construction modifications
  const auto cloned = node_data.Clone();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* cloned_node_data = static_cast<NodeData*>(cloned.get());

  // Assert: Verify clone preserves the modified flag state
  EXPECT_TRUE(
    cloned_node_data->flags_.GetEffectiveValue(SceneNodeFlags::kCastsShadows));
}

} // namespace
