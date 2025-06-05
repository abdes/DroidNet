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

//! Test enum for SceneFlags.
//! This enum defines a set of flags used for testing the SceneFlags class.
enum class TestFlag : uint8_t {
    kVisible, //!< Represents visibility status.
    kLocked, //!< Represents locked status.
    kSelected, //!< Represents selected status.
    kCount //!< Represents the total number of flags.
};
static_assert(SceneFlagEnum<TestFlag>);
[[maybe_unused]] auto constexpr to_string(const TestFlag& value) noexcept -> const char*
{
    switch (value) {
    case TestFlag::kVisible:
        return "Visible";
    case TestFlag::kLocked:
        return "Locked";
    case TestFlag::kSelected:
        return "Selected";

    case TestFlag::kCount:
        break; // Sentinel value, not used
    }
    return "__NotSupported__";
}

//------------------------------------------------------------------------------
// SceneFlag Single Flag Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlag.
class SceneFlagTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flag for each test
        flag_ = SceneFlag {};
    }

    // Helper: Verify all bits are in expected state
    void ExpectAllBitsState(bool effective, bool inherited, bool dirty, bool previous, bool pending)
    {
        EXPECT_EQ(flag_.GetEffectiveValueBit(), effective);
        EXPECT_EQ(flag_.GetInheritedBit(), inherited);
        EXPECT_EQ(flag_.GetDirtyBit(), dirty);
        EXPECT_EQ(flag_.GetPreviousValueBit(), previous);
        EXPECT_EQ(flag_.GetPendingValueBit(), pending);
    }

    SceneFlag flag_;
};

NOLINT_TEST_F(SceneFlagTest, DefaultConstruction_AllBitsAreFalse)
{
    // Arrange: Default constructed flag (done in SetUp)

    // Act: No action needed for default construction test

    // Assert: Verify all bits are false and raw value is zero
    ExpectAllBitsState(false, false, false, false, false);
    EXPECT_EQ(flag_.GetRaw(), 0);
}

NOLINT_TEST_F(SceneFlagTest, BitSetters_ModifyIndividualBitsCorrectly)
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
    ExpectAllBitsState(true, true, true, true, true);
}

NOLINT_TEST_F(SceneFlagTest, RawAccess_SetAndGetRawValueCorrectly)
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
    ExpectAllBitsState(false, false, false, false, false);
}

NOLINT_TEST_F(SceneFlagTest, SetLocalValue_MakesFlagDirtyAndSetsCorrectState)
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

NOLINT_TEST_F(SceneFlagTest, ProcessDirty_TransitionsEffectiveValueCorrectly)
{
    // Arrange: Set up flag for processing (inherited, with pending value)
    flag_.SetInheritedBit(true).SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.GetDirtyBit());

    // Act: Process the dirty flag
    auto result = flag_.ProcessDirty();

    // Assert: Flag should transition to new effective value and clear dirty state
    EXPECT_TRUE(result);
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());
    EXPECT_FALSE(flag_.GetDirtyBit());
}

NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParent_UpdatesInheritedFlagCorrectly)
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
    auto result = flag_.ProcessDirty();

    // Assert: Effective value should match parent (false) and previous should be preserved (true)
    EXPECT_TRUE(result);
    EXPECT_FALSE(flag_.GetEffectiveValueBit()); // New effective value from parent
    EXPECT_TRUE(flag_.GetPreviousValueBit()); // Previous value should be the old effective value (true)
    EXPECT_FALSE(flag_.GetDirtyBit()); // Should be clean after processing
}

NOLINT_TEST_F(SceneFlagTest, EqualityOperators_CompareCorrectly)
{
    // Arrange: Two default flags
    auto flag_a = SceneFlag {};
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

NOLINT_TEST_F(SceneFlagTest, StringConversion_ProducesNonEmptyString)
{
    // Arrange: Flag with some bits set
    flag_.SetEffectiveValueBit(true);

    // Act: Convert to string
    auto str = oxygen::scene::to_string(flag_);

    // Assert: String should not be empty
    EXPECT_FALSE(str.empty());
}

//------------------------------------------------------------------------------
// SceneFlag Advanced Behavior Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneFlagTest, SemanticEquality_EffectiveEqualsWorksCorrectly)
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

NOLINT_TEST_F(SceneFlagTest, SemanticEquality_DifferentEffectiveValuesNotEqual)
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

NOLINT_TEST_F(SceneFlagTest, SemanticEquality_DirtyFlagsNeverEqual)
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

NOLINT_TEST_F(SceneFlagTest, SetLocalValueOptimization_SameValueWhenDirtyIsNoOp)
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

NOLINT_TEST_F(SceneFlagTest, SetLocalValueOptimization_RevertToEffectiveClearsDirty)
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

NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentOptimization_SameValueIsNoOp)
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

NOLINT_TEST_F(SceneFlagTest, ProcessDirtyTransitionTracking_PreviousValuePreserved)
{
    // Arrange: Flag transitioning from false to true
    flag_.SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.IsDirty());

    // Act: Process the transition
    auto result = flag_.ProcessDirty();

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

//------------------------------------------------------------------------------
// SceneFlags Multi-Flag Container Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlags.
class SceneFlagsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flags container for each test
        flags_ = SceneFlags<TestFlag> {};
    }

    // Helper: Update a flag value from a simulated parent
    void UpdateFlagValueFromParent(const TestFlag flag, const bool value)
    {
        auto parent = SceneFlags<TestFlag> {}.SetFlag(flag, SceneFlag {}.SetEffectiveValueBit(value));
        flags_.UpdateValueFromParent(flag, parent);
    }

    // Helper: Verify all flags have expected effective values
    void ExpectAllFlagsEffectiveValue(bool expected_value)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
            auto flag = static_cast<TestFlag>(i);
            EXPECT_EQ(flags_.GetEffectiveValue(flag), expected_value);
        }
    }

    SceneFlags<TestFlag> flags_;
};

NOLINT_TEST_F(SceneFlagsTest, DefaultConstruction_AllFlagsAreFalse)
{
    // Arrange: Default constructed flags container (done in SetUp)

    // Act: Check all flag values

    // Assert: All flags should have false effective values by default
    ExpectAllFlagsEffectiveValue(false);
}

NOLINT_TEST_F(SceneFlagsTest, SetFlag_CompleteStateIsPreserved)
{
    // Arrange: Create a flag with all bits set
    auto complete_flag = SceneFlag {}
                             .SetEffectiveValueBit(true)
                             .SetInheritedBit(true)
                             .SetDirtyBit(true)
                             .SetPreviousValueBit(true);

    // Act: Set this complete flag state
    flags_.SetFlag(TestFlag::kLocked, complete_flag);

    // Assert: All flag state should be preserved exactly
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));
    EXPECT_TRUE(flags_.GetPreviousValue(TestFlag::kLocked));
}

NOLINT_TEST_F(SceneFlagsTest, SetLocalValue_MakesFlagLocalAndDirty)
{
    // Arrange: Clean flags container

    // Act: Set local value for visible flag
    flags_.SetLocalValue(TestFlag::kVisible, true);

    // Assert: Flag should be dirty and not inherited
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsInherited(TestFlag::kVisible));

    // Act: Process the dirty flag
    flags_.ProcessDirtyFlag(TestFlag::kVisible);

    // Assert: Flag should now have effective value and not be dirty
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsInherited(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kVisible));
}

NOLINT_TEST_F(SceneFlagsTest, InheritanceAndParentUpdate_WorksCorrectly)
{
    // Arrange: Set up flag as inherited from parent
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.ProcessDirtyFlag(TestFlag::kVisible);
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsInherited(TestFlag::kVisible));

    // Act: Enable inheritance for this flag
    flags_.SetInherited(TestFlag::kVisible, true);

    // Act: Update from parent with different value
    UpdateFlagValueFromParent(TestFlag::kVisible, false);

    // Assert: Flag should be dirty due to parent update
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));

    // Act: Process all dirty flags
    flags_.ProcessDirtyFlags();

    // Assert: Should now inherit parent value
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kVisible));
    EXPECT_FALSE(flags_.GetEffectiveValue(TestFlag::kVisible));
}

NOLINT_TEST_F(SceneFlagsTest, ProcessDirtyFlag_ReturnsTrueWhenProcessed)
{
    // Arrange: Make a flag dirty
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));

    // Act: Process the specific dirty flag
    auto result = flags_.ProcessDirtyFlag(TestFlag::kLocked);

    // Assert: Should return true and clear dirty state
    EXPECT_TRUE(result);
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kLocked));
}

NOLINT_TEST_F(SceneFlagsTest, RawAccess_PreservesCompleteState)
{
    // Arrange: Set up flags with various states
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();

    // Act: Get raw representation and create new container
    auto raw = flags_.Raw();
    auto other = SceneFlags<TestFlag> {};
    other.SetRaw(raw);

    // Assert: New container should match original exactly
    EXPECT_EQ(flags_, other);
}

NOLINT_TEST_F(SceneFlagsTest, Clear_ResetsAllFlagsToDefault)
{
    // Arrange: Set up flags with various values
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();

    // Act: Clear all flags
    flags_.Clear();

    // Assert: All flags should be false
    ExpectAllFlagsEffectiveValue(false);
}

NOLINT_TEST_F(SceneFlagsTest, Equality_WorksCorrectly)
{
    // Arrange: Two containers with same state
    auto flags_a = SceneFlags<TestFlag> {};
    auto flags_b = SceneFlags<TestFlag> {};
    flags_a.SetLocalValue(TestFlag::kVisible, true);
    flags_b.SetLocalValue(TestFlag::kVisible, true);

    // Act & Assert: Should be equal
    EXPECT_TRUE(flags_a == flags_b);

    // Act: Make them different
    flags_b.SetLocalValue(TestFlag::kLocked, true);

    // Assert: Should not be equal
    EXPECT_FALSE(flags_a == flags_b);
    EXPECT_TRUE(flags_a != flags_b);
}

NOLINT_TEST_F(SceneFlagsTest, OutOfBoundsAccess_DoesNotThrow)
{
    // Arrange: Invalid flag enum value
    auto bogus = static_cast<TestFlag>(99);

    // Act & Assert: Should not throw (graceful degradation)
    NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.GetEffectiveValue(bogus));
}

//------------------------------------------------------------------------------
// SceneFlags Iterator and Range Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneFlagsTest, Iterator_CoversAllFlags)
{
    // Arrange: Default flags container

    // Act: Iterate through all flags and mark them as seen
    auto seen = std::array<bool, static_cast<std::size_t>(TestFlag::kCount)> {};
    for (auto flag : flags_ | std::views::keys) {
        seen[static_cast<std::size_t>(flag)] = true;
    }

    // Assert: All flags should have been seen
    for (bool was_seen : seen) {
        EXPECT_TRUE(was_seen);
    }
}

NOLINT_TEST_F(SceneFlagsTest, DirtyFlagsRange_ShowsOnlyDirtyFlags)
{
    // Arrange: Make some flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);

    // Act: Count dirty flags using range
    std::size_t dirty_count = 0;
    for (auto flag : flags_.dirty_flags()) {
        (void)flag;
        ++dirty_count;
    }

    // Assert: Should find exactly 2 dirty flags
    EXPECT_EQ(dirty_count, 2);
}

NOLINT_TEST_F(SceneFlagsTest, InheritedFlagsAdapter_ShowsOnlyInheritedFlags)
{
    // Arrange: Set up some flags as inherited
    flags_.Clear();
    flags_.SetInherited(TestFlag::kVisible, true);
    flags_.SetInherited(TestFlag::kLocked, false);
    flags_.SetInherited(TestFlag::kSelected, true);

    // Act: Collect inherited flags
    auto inherited = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::inherited_flags(flags_)) {
        inherited.push_back(flag);
    }

    // Assert: Should find only the inherited flags
    EXPECT_THAT(inherited, testing::UnorderedElementsAre(TestFlag::kVisible, TestFlag::kSelected));
}

NOLINT_TEST_F(SceneFlagsTest, EffectiveTrueFlagsAdapter_ShowsOnlyTrueFlags)
{
    // Arrange: Set up flags with mixed effective values
    flags_.Clear();
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);
    flags_.SetLocalValue(TestFlag::kLocked, false);
    flags_.ProcessDirtyFlags();

    // Act: Collect flags with true effective values
    auto true_flags = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::effective_true_flags(flags_)) {
        true_flags.push_back(flag);
    }

    // Assert: Should find only flags with true effective values
    EXPECT_THAT(true_flags, testing::UnorderedElementsAre(TestFlag::kVisible, TestFlag::kSelected));
}

NOLINT_TEST_F(SceneFlagsTest, EffectiveFalseFlagsAdapter_ShowsOnlyFalseFlags)
{
    // Arrange: Set up flags with mixed effective values
    flags_.Clear();
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);
    flags_.SetLocalValue(TestFlag::kLocked, false);
    flags_.ProcessDirtyFlags();

    // Act: Collect flags with false effective values
    auto false_flags = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::effective_false_flags(flags_)) {
        false_flags.push_back(flag);
    }

    // Assert: Should find only flags with false effective values
    EXPECT_THAT(false_flags, testing::UnorderedElementsAre(TestFlag::kLocked));
}

NOLINT_TEST_F(SceneFlagsTest, Iterator_EmptyEnumHandledCorrectly)
{
    // Arrange: Test with enum that has no actual flags
    enum class EmptyTestFlag : uint8_t { kCount };
    static_assert(SceneFlagEnum<EmptyTestFlag>);
    auto zero_flags = SceneFlags<EmptyTestFlag> {};

    // Act: Iterate over empty enum
    std::size_t count = 0;
    for (auto item : zero_flags) {
        (void)item;
        count++;
    }

    // Assert: Should handle empty enum gracefully
    EXPECT_EQ(zero_flags.begin(), zero_flags.end());
    EXPECT_EQ(count, 0);
}

//------------------------------------------------------------------------------
// SceneFlags Bulk Operations Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneFlagsTest, BulkSetLocalValue_AllFlagsModified)
{
    // Arrange: Clean flags container

    // Act: Set all flags to true using bulk operation
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, true);
    }
    flags_.ProcessDirtyFlags();

    // Assert: All flags should now be true
    ExpectAllFlagsEffectiveValue(true);
}

NOLINT_TEST_F(SceneFlagsTest, SetInheritedAll_AllFlagsInheritFromParent)
{
    // Arrange: Set up flags with local values
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, true);
    }
    flags_.ProcessDirtyFlags();

    // Act: Set all flags to inherit from parent
    flags_.SetInheritedAll(true);

    // Act: Update all from empty parent (all false values)
    flags_.UpdateAllInheritFromParent(SceneFlags<TestFlag> {});

    // Assert: All flags should become dirty due to parent update
    EXPECT_GT(flags_.CountDirtyFlags(), 0);
}

NOLINT_TEST_F(SceneFlagsTest, ClearDirtyFlags_OnlyDirtyBitsCleared)
{
    // Arrange: Make some flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));

    // Act: Clear only dirty bits
    flags_.ClearDirtyFlags();

    // Assert: Dirty bits should be cleared but other state preserved
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kLocked));
}

NOLINT_TEST_F(SceneFlagsTest, CountDirtyFlags_ReturnsCorrectCount)
{
    // Arrange: Clean flags
    EXPECT_EQ(flags_.CountDirtyFlags(), 0);

    // Act: Make some flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);

    // Assert: Should count exactly 2 dirty flags
    EXPECT_EQ(flags_.CountDirtyFlags(), 2);

    // Act: Process dirty flags
    flags_.ProcessDirtyFlags();

    // Assert: Should have no dirty flags after processing
    EXPECT_EQ(flags_.CountDirtyFlags(), 0);
}

NOLINT_TEST_F(SceneFlagsTest, ProcessDirtyFlags_ReturnsTrueWhenFlagsProcessed)
{
    // Arrange: Make flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);

    // Act: Process all dirty flags
    auto result = flags_.ProcessDirtyFlags();

    // Assert: Should return true and apply all changes
    if (result) {
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kLocked));
        EXPECT_EQ(flags_.CountDirtyFlags(), 0);
    }
}

//------------------------------------------------------------------------------
// SceneFlags Copy/Move Semantics Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(SceneFlagsTest, CopyConstruction_PreservesState)
{
    // Arrange: Set up flags with specific state
    auto original = SceneFlags<TestFlag> {};
    original.SetLocalValue(TestFlag::kVisible, true);

    // Act: Copy construct new container
    auto copy = SceneFlags<TestFlag>(original);

    // Assert: Copy should match original exactly
    EXPECT_EQ(original, copy);
}

NOLINT_TEST_F(SceneFlagsTest, CopyAssignment_PreservesState)
{
    // Arrange: Set up source and target containers
    auto source = SceneFlags<TestFlag> {};
    source.SetLocalValue(TestFlag::kVisible, true);
    auto target = SceneFlags<TestFlag> {};

    // Act: Copy assign
    target = source;

    // Assert: Target should match source
    EXPECT_EQ(source, target);
}

NOLINT_TEST_F(SceneFlagsTest, MoveConstruction_TransfersState)
{
    // Arrange: Set up flags with state
    auto original = SceneFlags<TestFlag> {};
    original.SetLocalValue(TestFlag::kVisible, true);
    auto expected = original; // Copy for comparison

    // Act: Move construct
    auto moved = SceneFlags<TestFlag>(std::move(original));

    // Assert: Moved container should have expected state
    EXPECT_EQ(moved, expected);
}

NOLINT_TEST_F(SceneFlagsTest, MoveAssignment_TransfersState)
{
    // Arrange: Set up source and target
    auto source = SceneFlags<TestFlag> {};
    source.SetLocalValue(TestFlag::kVisible, true);
    auto expected = source; // Copy for comparison
    auto target = SceneFlags<TestFlag> {};

    // Act: Move assign
    target = std::move(source);

    // Assert: Target should have expected state
    EXPECT_EQ(target, expected);
}

//------------------------------------------------------------------------------
// AtomicSceneFlags Thread-Safe Container Tests
//------------------------------------------------------------------------------

//! Test fixture for AtomicSceneFlags.
class AtomicSceneFlagsTest : public ::testing::Test {
protected:
    AtomicSceneFlags<TestFlag> atomic_flags_ {};
};

NOLINT_TEST_F(AtomicSceneFlagsTest, StoreAndLoad_PreservesState)
{
    // Arrange: Create flags with specific state
    auto flags = SceneFlags<TestFlag> {};
    flags.SetLocalValue(TestFlag::kVisible, true);
    flags.ProcessDirtyFlags();

    // Act: Store flags atomically
    atomic_flags_.Store(flags);

    // Act: Load flags atomically
    auto loaded = atomic_flags_.Load();

    // Assert: Loaded flags should match stored flags
    EXPECT_TRUE(loaded.GetEffectiveValue(TestFlag::kVisible));
}

NOLINT_TEST_F(AtomicSceneFlagsTest, Exchange_ReturnsOldValueAndSetsNew)
{
    // Arrange: Set up initial state and new state
    auto initial_flags = SceneFlags<TestFlag> {};
    initial_flags.SetLocalValue(TestFlag::kVisible, true);
    initial_flags.ProcessDirtyFlags();
    atomic_flags_.Store(initial_flags);

    auto new_flags = SceneFlags<TestFlag> {};
    new_flags.SetLocalValue(TestFlag::kSelected, true);
    new_flags.SetLocalValue(TestFlag::kVisible, false);
    new_flags.ProcessDirtyFlags();

    // Act: Exchange old for new
    auto returned_flags = atomic_flags_.Exchange(new_flags);

    // Assert: Should return old value and store new value
    EXPECT_TRUE(returned_flags.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_EQ(atomic_flags_.Load(), new_flags);
}

NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeWeak_SucceedsWithCorrectExpected)
{
    // Arrange: Set up expected and desired states
    auto expected = SceneFlags<TestFlag> {};
    auto desired = SceneFlags<TestFlag> {};
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    // Act: Attempt weak compare-exchange with correct expected value
    auto result = atomic_flags_.CompareExchangeWeak(expected, desired);

    // Assert: Should succeed and update to desired value
    EXPECT_TRUE(result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeStrong_SucceedsWithCorrectExpected)
{
    // Arrange: Set up expected and desired states
    auto expected = SceneFlags<TestFlag> {};
    auto desired = SceneFlags<TestFlag> {};
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    // Act: Attempt strong compare-exchange with correct expected value
    auto result = atomic_flags_.CompareExchangeStrong(expected, desired);

    // Assert: Should succeed and update to desired value
    EXPECT_TRUE(result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeWeak_FailsAndUpdatesExpected)
{
    // Arrange: Set up mismatched expected value
    auto current_value = SceneFlags<TestFlag> {};
    current_value.SetLocalValue(TestFlag::kVisible, true);
    current_value.SetLocalValue(TestFlag::kLocked, false);
    current_value.ProcessDirtyFlags();

    auto wrong_expected = SceneFlags<TestFlag> {};
    wrong_expected.SetLocalValue(TestFlag::kVisible, false);
    wrong_expected.SetLocalValue(TestFlag::kLocked, true);
    wrong_expected.ProcessDirtyFlags();

    auto desired = SceneFlags<TestFlag> {};
    desired.SetLocalValue(TestFlag::kSelected, true);
    desired.ProcessDirtyFlags();

    atomic_flags_.Store(current_value);

    // Act: Attempt compare-exchange with wrong expected value
    auto result = atomic_flags_.CompareExchangeWeak(wrong_expected, desired);

    // Assert: Should fail, update expected, and leave current unchanged
    EXPECT_FALSE(result);
    EXPECT_EQ(wrong_expected, current_value);
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeStrong_FailsAndUpdatesExpected)
{
    // Arrange: Set up mismatched expected value
    auto current_value = SceneFlags<TestFlag> {};
    current_value.SetLocalValue(TestFlag::kVisible, true);
    current_value.SetLocalValue(TestFlag::kLocked, false);
    current_value.ProcessDirtyFlags();

    auto wrong_expected = SceneFlags<TestFlag> {};
    wrong_expected.SetLocalValue(TestFlag::kSelected, true);
    wrong_expected.ProcessDirtyFlags();

    auto desired = SceneFlags<TestFlag> {};
    desired.SetLocalValue(TestFlag::kVisible, false);
    desired.SetLocalValue(TestFlag::kLocked, false);
    desired.ProcessDirtyFlags();

    atomic_flags_.Store(current_value);

    // Act: Attempt strong compare-exchange with wrong expected value
    auto result = atomic_flags_.CompareExchangeStrong(wrong_expected, desired);

    // Assert: Should fail, update expected, and leave current unchanged
    EXPECT_FALSE(result);
    EXPECT_EQ(wrong_expected, current_value);
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

} // namespace
