//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::AtomicSceneFlags;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlagEnum;
using oxygen::scene::SceneFlags;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

//------------------------------------------------------------------------------
// Common Helpers for SceneFlag Tests
//------------------------------------------------------------------------------

// Helper: Verify all bits are in expected state
void ExpectAllBitsState(
    const SceneFlag& flag,
    const bool effective,
    const bool inherited,
    const bool dirty,
    const bool previous,
    const bool pending)
{
    EXPECT_EQ(flag.GetEffectiveValueBit(), effective);
    EXPECT_EQ(flag.GetInheritedBit(), inherited);
    EXPECT_EQ(flag.GetDirtyBit(), dirty);
    EXPECT_EQ(flag.GetPreviousValueBit(), previous);
    EXPECT_EQ(flag.GetPendingValueBit(), pending);
}

//------------------------------------------------------------------------------
// SceneFlag Basic Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlag basic functionality.
class SceneFlagBasicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flag for each test
        flag_ = SceneFlag {};
    }

    SceneFlag flag_;
};

NOLINT_TEST_F(SceneFlagBasicTest, DefaultConstruction_AllBitsAreFalse)
{
    // Arrange: Default constructed flag (done in SetUp)

    // Act: No action needed for default construction test

    // Assert: Verify all bits are false and raw value is zero
    ExpectAllBitsState(flag_, false, false, false, false, false);
    EXPECT_EQ(flag_.GetRaw(), 0);
}

NOLINT_TEST_F(SceneFlagBasicTest, BitSetters_ModifyIndividualBitsCorrectly)
{
    // Arrange: Start with default flag (all bits false)

    // Act: Set each bit individually and verify
    flag_.SetEffectiveValueBit(true);

    // Assert: Only effective value bit should be true
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());

    // Act: Set inherited bit
    flag_.SetInheritedBit(true);

    // Assert: Both effective and inherited should be true
    EXPECT_TRUE(flag_.GetInheritedBit());

    // Act: Set remaining bits
    flag_.SetDirtyBit(true);
    flag_.SetPreviousValueBit(true);
    flag_.SetPendingValueBit(true);

    // Assert: All bits should now be true
    ExpectAllBitsState(flag_, true, true, true, true, true);
}

NOLINT_TEST_F(SceneFlagBasicTest, RawAccess_SetAndGetRawValueCorrectly)
{
    // Arrange: Set all bits to true
    flag_.SetEffectiveValueBit(true);
    flag_.SetInheritedBit(true);
    flag_.SetDirtyBit(true);
    flag_.SetPreviousValueBit(true);
    flag_.SetPendingValueBit(true);

    // Act: Reset using raw value
    flag_.SetRaw(0);

    // Assert: All bits should be false after raw reset
    ExpectAllBitsState(flag_, false, false, false, false, false);
}

NOLINT_TEST_F(SceneFlagBasicTest, SetLocalValue_MakesFlagDirtyAndSetsCorrectState)
{
    // Arrange: Flag with inherited bit set and effective value false
    flag_.SetInheritedBit(true).SetEffectiveValueBit(false);

    // Act: Set local value to true
    flag_.SetLocalValue(true);

    // Assert: Flag should be dirty and inherited should be disabled
    EXPECT_TRUE(flag_.GetDirtyBit());
    EXPECT_FALSE(flag_.GetInheritedBit());
    EXPECT_TRUE(flag_.GetPendingValueBit());
}

NOLINT_TEST_F(SceneFlagBasicTest, ProcessDirty_TransitionsEffectiveValueCorrectly)
{
    // Arrange: Set up flag for processing (inherited, with pending value)
    flag_.SetInheritedBit(true).SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.GetDirtyBit());

    // Act: Process the dirty flag
    const auto result = flag_.ProcessDirty();

    // Assert: Flag should transition to new effective value and clear dirty state
    EXPECT_TRUE(result);
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());
    EXPECT_FALSE(flag_.GetDirtyBit());
}

NOLINT_TEST_F(SceneFlagBasicTest, EqualityOperators_CompareCorrectly)
{
    // Arrange: Two default flags
    constexpr auto flag_a = SceneFlag {};
    auto flag_b = SceneFlag {};

    // Act & Assert: Default flags should be equal
    EXPECT_TRUE(flag_b == flag_a);
    EXPECT_FALSE(flag_b != flag_a);

    // Act: Modify one flag
    flag_b.SetEffectiveValueBit(true);

    // Assert: Modified flag should not be equal
    EXPECT_FALSE(flag_b == flag_a);
    EXPECT_TRUE(flag_b != flag_a);
}

NOLINT_TEST_F(SceneFlagBasicTest, StringConversion_ProducesNonEmptyString)
{
    // Arrange: Flag with some bits set
    flag_.SetEffectiveValueBit(true);

    // Act: Convert to string
    const auto str = oxygen::scene::to_string(flag_);

    // Assert: String should not be empty
    EXPECT_FALSE(str.empty());
}

NOLINT_TEST_F(SceneFlagBasicTest, SemanticEquality_EffectiveEqualsWorksCorrectly)
{
    // Arrange: Two flags with same effective value, both clean
    auto flag1 = SceneFlag {};
    auto flag2 = SceneFlag {};
    flag1.SetEffectiveValueBit(true).SetDirtyBit(false);
    flag2.SetEffectiveValueBit(true).SetDirtyBit(false);

    // Act & Assert: Clean flags with same effective value should be semantically equal
    EXPECT_TRUE(flag1.EffectiveEquals(flag2));
    EXPECT_FALSE(flag1.EffectiveNotEquals(flag2));
}

NOLINT_TEST_F(SceneFlagBasicTest, SemanticEquality_DifferentEffectiveValuesNotEqual)
{
    // Arrange: Two clean flags with different effective values
    auto flag1 = SceneFlag {};
    auto flag2 = SceneFlag {};
    flag1.SetEffectiveValueBit(true).SetDirtyBit(false);
    flag2.SetEffectiveValueBit(false).SetDirtyBit(false);

    // Act & Assert: Clean flags with different effective values should not be equal
    EXPECT_FALSE(flag1.EffectiveEquals(flag2));
    EXPECT_TRUE(flag1.EffectiveNotEquals(flag2));
}

NOLINT_TEST_F(SceneFlagBasicTest, ConstructorFromRawBits_InitializesCorrectly)
{
    // Arrange: Raw bit pattern with all bits set (0b11111 = 31)
    constexpr std::uint8_t all_bits_set = 0b11111;

    // Act: Construct flag from raw bits
    constexpr auto flag = SceneFlag(all_bits_set);

    // Assert: All bits should be set correctly
    ExpectAllBitsState(flag, true, true, true, true, true);
    EXPECT_EQ(flag.GetRaw(), all_bits_set);
}

NOLINT_TEST_F(SceneFlagBasicTest, ConstructorFromRawBits_MasksUpperBits)
{
    // Arrange: Raw bit pattern with upper bits set (0b11100000)
    constexpr std::uint8_t upper_bits_set = 0b11100000;

    // Act: Construct flag from raw bits
    constexpr auto flag = SceneFlag(upper_bits_set);

    // Assert: Upper bits should be masked out, all flag bits should be false
    ExpectAllBitsState(flag, false, false, false, false, false);
    EXPECT_EQ(flag.GetRaw(), 0);
}

NOLINT_TEST_F(SceneFlagBasicTest, ConstructorFromRawBits_SpecificBitPattern)
{
    // Arrange: Raw bit pattern with specific bits set (effective=true, dirty=true)
    constexpr std::uint8_t specific_bits = 0b01001; // dirty (bit 3) and effective (bit 0)

    // Act: Construct flag from raw bits
    constexpr auto flag = SceneFlag(specific_bits);

    // Assert: Only specified bits should be set
    ExpectAllBitsState(flag, true, false, true, false, false);
    EXPECT_EQ(flag.GetRaw(), specific_bits);
}

NOLINT_TEST_F(SceneFlagBasicTest, GetEffectiveValue_WrapperAroundGetEffectiveValueBit)
{
    // Arrange: Flag with effective value bit set to true
    flag_.SetEffectiveValueBit(true);

    // Act & Assert: GetEffectiveValue should return same as GetEffectiveValueBit
    EXPECT_EQ(flag_.GetEffectiveValue(), flag_.GetEffectiveValueBit());
    EXPECT_TRUE(flag_.GetEffectiveValue());

    // Act: Set effective value bit to false
    flag_.SetEffectiveValueBit(false);

    // Assert: GetEffectiveValue should return false
    EXPECT_EQ(flag_.GetEffectiveValue(), flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetEffectiveValue());
}

NOLINT_TEST_F(SceneFlagBasicTest, GetPendingValue_WrapperAroundGetPendingValueBit)
{
    // Arrange: Flag with pending value bit set to true
    flag_.SetPendingValueBit(true);

    // Act & Assert: GetPendingValue should return same as GetPendingValueBit
    EXPECT_EQ(flag_.GetPendingValue(), flag_.GetPendingValueBit());
    EXPECT_TRUE(flag_.GetPendingValue());

    // Act: Set pending value bit to false
    flag_.SetPendingValueBit(false);

    // Assert: GetPendingValue should return false
    EXPECT_EQ(flag_.GetPendingValue(), flag_.GetPendingValueBit());
    EXPECT_FALSE(flag_.GetPendingValue());
}

NOLINT_TEST_F(SceneFlagBasicTest, GetPreviousValue_WrapperAroundGetPreviousValueBit)
{
    // Arrange: Flag with previous value bit set to true
    flag_.SetPreviousValueBit(true);

    // Act & Assert: GetPreviousValue should return same as GetPreviousValueBit
    EXPECT_EQ(flag_.GetPreviousValue(), flag_.GetPreviousValueBit());
    EXPECT_TRUE(flag_.GetPreviousValue());

    // Act: Set previous value bit to false
    flag_.SetPreviousValueBit(false);

    // Assert: GetPreviousValue should return false
    EXPECT_EQ(flag_.GetPreviousValue(), flag_.GetPreviousValueBit());
    EXPECT_FALSE(flag_.GetPreviousValue());
}

NOLINT_TEST_F(SceneFlagBasicTest, IsDirty_WrapperAroundGetDirtyBit)
{
    // Arrange: Flag with dirty bit set to true
    flag_.SetDirtyBit(true);

    // Act & Assert: IsDirty should return same as GetDirtyBit
    EXPECT_EQ(flag_.IsDirty(), flag_.GetDirtyBit());
    EXPECT_TRUE(flag_.IsDirty());

    // Act: Set dirty bit to false
    flag_.SetDirtyBit(false);

    // Assert: IsDirty should return false
    EXPECT_EQ(flag_.IsDirty(), flag_.GetDirtyBit());
    EXPECT_FALSE(flag_.IsDirty());
}

//------------------------------------------------------------------------------
// SceneFlag Error Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlag error scenarios.
class [[maybe_unused]] SceneFlagErrorTest : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flag for each test
        flag_ = SceneFlag {};
    }

    SceneFlag flag_;
};

// Note: Currently no error test cases - this fixture is reserved for future error scenarios

//------------------------------------------------------------------------------
// SceneFlag Inheritance Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlag inheritance functionality.
class SceneFlagInheritanceTest : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flag for each test
        flag_ = SceneFlag {};
    }

    SceneFlag flag_;
};

NOLINT_TEST_F(SceneFlagInheritanceTest, UpdateValueFromParent_UpdatesInheritedFlagCorrectly)
{
    // Arrange: Set up inherited flag with initial effective value of true
    flag_.SetInheritedBit(true);
    flag_.SetEffectiveValueBit(true); // Start with true effective value
    flag_.SetPendingValueBit(true); // Pending should match effective initially
    flag_.SetDirtyBit(false); // Start clean

    // Act: Update from parent with different value (false)
    flag_.UpdateValueFromParent(false);

    // Assert: Flag should become dirty due to parent update changing the value
    EXPECT_TRUE(flag_.GetDirtyBit());
    EXPECT_FALSE(flag_.GetPendingValueBit()); // Pending should now be false from parent

    // Act: Process the dirty flag to apply parent value
    const auto result = flag_.ProcessDirty();

    // Assert: Effective value should match parent (false) and previous should be preserved (true)
    EXPECT_TRUE(result);
    EXPECT_FALSE(flag_.GetEffectiveValueBit()); // New effective value from parent
    EXPECT_TRUE(flag_.GetPreviousValueBit()); // Previous value should be the old effective value (true)
    EXPECT_FALSE(flag_.GetDirtyBit()); // Should be clean after processing
}

NOLINT_TEST_F(SceneFlagInheritanceTest, IsInherited_WrapperAroundGetInheritedBit)
{
    // Arrange: Flag with inherited bit set to true
    flag_.SetInheritedBit(true);

    // Act & Assert: IsInherited should return same as GetInheritedBit
    EXPECT_EQ(flag_.IsInherited(), flag_.GetInheritedBit());
    EXPECT_TRUE(flag_.IsInherited());

    // Act: Set inherited bit to false
    flag_.SetInheritedBit(false);

    // Assert: IsInherited should return false
    EXPECT_EQ(flag_.IsInherited(), flag_.GetInheritedBit());
    EXPECT_FALSE(flag_.IsInherited());
}

NOLINT_TEST_F(SceneFlagInheritanceTest, SetInherited_EnablesInheritanceAndMarksDirty)
{
    // Arrange: Clean flag
    EXPECT_FALSE(flag_.IsInherited());
    EXPECT_FALSE(flag_.IsDirty());

    // Act: Enable inheritance
    flag_.SetInherited(true);

    // Assert: Flag should be inherited and dirty
    EXPECT_TRUE(flag_.IsInherited());
    EXPECT_TRUE(flag_.IsDirty());
}

NOLINT_TEST_F(SceneFlagInheritanceTest, SetInherited_DisablesInheritanceAndMarksDirty)
{
    // Arrange: Flag with inheritance enabled
    flag_.SetInheritedBit(true).SetDirtyBit(false);
    EXPECT_TRUE(flag_.IsInherited());
    EXPECT_FALSE(flag_.IsDirty());

    // Act: Disable inheritance
    flag_.SetInherited(false);

    // Assert: Flag should not be inherited and should be dirty
    EXPECT_FALSE(flag_.IsInherited());
    EXPECT_TRUE(flag_.IsDirty());
}

NOLINT_TEST_F(SceneFlagInheritanceTest, UpdateValueFromParentOptimization_SameValueIsNoOp)
{
    // Arrange: Inherited flag with current pending value
    flag_.SetInheritedBit(true);
    flag_.SetPendingValueBit(false);
    flag_.SetDirtyBit(false);

    // Act: Update from parent with same value
    flag_.UpdateValueFromParent(false);

    // Assert: Should remain unchanged (optimization case)
    EXPECT_FALSE(flag_.IsDirty());
}

//------------------------------------------------------------------------------
// SceneFlag EdgeCase Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlag edge case scenarios.
class SceneFlagEdgeCaseTest : public testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flag for each test
        flag_ = SceneFlag {};
    }

    SceneFlag flag_;
};

NOLINT_TEST_F(SceneFlagEdgeCaseTest, SemanticEquality_DirtyFlagsNeverEqual)
{
    // Arrange: Two flags with same effective value, one dirty
    auto flag1 = SceneFlag {};
    auto flag2 = SceneFlag {};
    flag1.SetEffectiveValueBit(true).SetDirtyBit(true);
    flag2.SetEffectiveValueBit(true).SetDirtyBit(false);

    // Act & Assert: Dirty flag should never be semantically equal
    EXPECT_FALSE(flag1.EffectiveEquals(flag2));
    EXPECT_TRUE(flag1.EffectiveNotEquals(flag2));
}

NOLINT_TEST_F(SceneFlagEdgeCaseTest, SetLocalValueOptimization_SameValueWhenDirtyIsNoOp)
{
    // Arrange: Flag that's already dirty with pending value
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.IsDirty());
    EXPECT_TRUE(flag_.GetPendingValueBit());

    // Act: Set same local value again
    flag_.SetLocalValue(true);

    // Assert: Should remain in same state (optimization case)
    EXPECT_TRUE(flag_.IsDirty());
    EXPECT_TRUE(flag_.GetPendingValueBit());
}

NOLINT_TEST_F(SceneFlagEdgeCaseTest, SetLocalValueOptimization_RevertToEffectiveClearsDirty)
{
    // Arrange: Flag with effective value false, then set to true
    flag_.SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.IsDirty());

    // Act: Revert to original effective value
    flag_.SetLocalValue(false);

    // Assert: Should clear dirty bit (optimization case)
    EXPECT_FALSE(flag_.IsDirty());
    EXPECT_FALSE(flag_.GetPendingValueBit());
}

NOLINT_TEST_F(SceneFlagEdgeCaseTest, ProcessDirtyTransitionTracking_PreviousValuePreserved)
{
    // Arrange: Flag transitioning from false to true
    flag_.SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.IsDirty());

    // Act: Process the transition
    const auto result = flag_.ProcessDirty();

    // Assert: Transition should be tracked correctly
    EXPECT_TRUE(result);
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetPreviousValueBit()); // Previous was false

    // Act: Transition back to false
    flag_.SetLocalValue(false);
    flag_.ProcessDirty();

    // Assert: Previous value should now reflect the true state
    EXPECT_FALSE(flag_.GetEffectiveValueBit());
    EXPECT_TRUE(flag_.GetPreviousValueBit()); // Previous was true
}

} // namespace
