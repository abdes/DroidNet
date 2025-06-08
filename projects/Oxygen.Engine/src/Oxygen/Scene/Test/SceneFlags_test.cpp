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

#include "./Mocks/TestFlag.h"

using oxygen::scene::AtomicSceneFlags;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneFlagEnum;
using oxygen::scene::SceneFlags;
using oxygen::scene::testing::TestFlag;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

//------------------------------------------------------------------------------
// Common Helpers for SceneFlags Tests
//------------------------------------------------------------------------------

// Helper: Update a flag value from a simulated parent
void UpdateFlagValueFromParent(SceneFlags<TestFlag>& flags, const TestFlag flag, const bool value)
{
    const auto parent = SceneFlags<TestFlag> {}.SetFlag(flag, SceneFlag {}.SetEffectiveValueBit(value));
    flags.UpdateValueFromParent(flag, parent);
}

// Helper: Verify all flags have expected effective values
void ExpectAllFlagsEffectiveValue(const SceneFlags<TestFlag>& flags, const bool expected_value)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_EQ(flags.GetEffectiveValue(flag), expected_value);
    }
}

//------------------------------------------------------------------------------
// SceneFlags Basic Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlags basic functionality.
class SceneFlagsBasicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flags container for each test
        flags_ = SceneFlags<TestFlag> {};
    }

    SceneFlags<TestFlag> flags_;
};

NOLINT_TEST_F(SceneFlagsBasicTest, DefaultConstruction_AllFlagsAreFalse)
{
    // Arrange: Default constructed flags container (done in SetUp)

    // Act: Check all flag values

    // Assert: All flags should have false effective values by default
    ExpectAllFlagsEffectiveValue(flags_, false);
}

NOLINT_TEST_F(SceneFlagsBasicTest, SetFlag_CompleteStateIsPreserved)
{
    // Arrange: Create a flag with all bits set
    constexpr auto complete_flag = SceneFlag {}
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

NOLINT_TEST_F(SceneFlagsBasicTest, SetLocalValue_MakesFlagLocalAndDirty)
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

NOLINT_TEST_F(SceneFlagsBasicTest, ProcessDirtyFlag_ReturnsTrueWhenProcessed)
{
    // Arrange: Make a flag dirty
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));

    // Act: Process the specific dirty flag
    const auto result = flags_.ProcessDirtyFlag(TestFlag::kLocked);

    // Assert: Should return true and clear dirty state
    EXPECT_TRUE(result);
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kLocked));
}

NOLINT_TEST_F(SceneFlagsBasicTest, RawAccess_PreservesCompleteState)
{
    // Arrange: Set up flags with various states
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();

    // Act: Get raw representation and create new container
    const auto raw = flags_.Raw();
    auto other = SceneFlags<TestFlag> {};
    other.SetRaw(raw);

    // Assert: New container should match original exactly
    EXPECT_EQ(flags_, other);
}

NOLINT_TEST_F(SceneFlagsBasicTest, Clear_ResetsAllFlagsToDefault)
{
    // Arrange: Set up flags with various values
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();

    // Act: Clear all flags
    flags_.Clear();

    // Assert: All flags should be false
    ExpectAllFlagsEffectiveValue(flags_, false);
}

NOLINT_TEST_F(SceneFlagsBasicTest, Equality_WorksCorrectly)
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

NOLINT_TEST_F(SceneFlagsBasicTest, Iterator_CoversAllFlags)
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

NOLINT_TEST_F(SceneFlagsBasicTest, DirtyFlagsRange_ShowsOnlyDirtyFlags)
{
    // Arrange: Make some flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);

    // Act: Count dirty flags using range
    std::size_t dirty_count = 0;
    for (const auto flag : flags_.dirty_flags()) {
        (void)flag;
        ++dirty_count;
    }

    // Assert: Should find exactly 2 dirty flags
    EXPECT_EQ(dirty_count, 2);
}

NOLINT_TEST_F(SceneFlagsBasicTest, EffectiveTrueFlagsAdapter_ShowsOnlyTrueFlags)
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

NOLINT_TEST_F(SceneFlagsBasicTest, EffectiveFalseFlagsAdapter_ShowsOnlyFalseFlags)
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

NOLINT_TEST_F(SceneFlagsBasicTest, BulkSetLocalValue_AllFlagsModified)
{
    // Arrange: Clean flags container

    // Act: Set all flags to true using bulk operation
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, true);
    }
    flags_.ProcessDirtyFlags();

    // Assert: All flags should now be true
    ExpectAllFlagsEffectiveValue(flags_, true);
}

NOLINT_TEST_F(SceneFlagsBasicTest, ClearDirtyFlags_OnlyDirtyBitsCleared)
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

NOLINT_TEST_F(SceneFlagsBasicTest, CountDirtyFlags_ReturnsCorrectCount)
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

NOLINT_TEST_F(SceneFlagsBasicTest, ProcessDirtyFlags_ReturnsTrueWhenFlagsProcessed)
{
    // Arrange: Make flags dirty
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);

    // Act: Process all dirty flags
    // ReSharper disable once CppTooWideScope
    const auto result = flags_.ProcessDirtyFlags();

    // Assert: Should return true and apply all changes
    if (result) {
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kLocked));
        EXPECT_EQ(flags_.CountDirtyFlags(), 0);
    }
}

NOLINT_TEST_F(SceneFlagsBasicTest, CopyConstruction_PreservesState)
{
    // Arrange: Set up flags with specific state
    auto original = SceneFlags<TestFlag> {};
    original.SetLocalValue(TestFlag::kVisible, true);

    // Act: Copy construct new container
    const auto copy = SceneFlags(original);

    // Assert: Copy should match original exactly
    EXPECT_EQ(original, copy);
}

NOLINT_TEST_F(SceneFlagsBasicTest, CopyAssignment_PreservesState)
{
    // Arrange: Set up source and target containers
    auto source = SceneFlags<TestFlag> {};
    source.SetLocalValue(TestFlag::kVisible, true);

    // Act: Copy assign
    const auto target = source;

    // Assert: Target should match source
    EXPECT_EQ(source, target);
}

NOLINT_TEST_F(SceneFlagsBasicTest, MoveConstruction_TransfersState)
{
    // Arrange: Set up flags with state
    auto original = SceneFlags<TestFlag> {};
    original.SetLocalValue(TestFlag::kVisible, true);
    const auto expected = original; // Copy for comparison

    // Act: Move construct
    const auto moved = SceneFlags(std::move(original)); // NOLINT(performance-move-const-arg)

    // Assert: Moved container should have expected state
    EXPECT_EQ(moved, expected);
}

NOLINT_TEST_F(SceneFlagsBasicTest, MoveAssignment_TransfersState)
{
    // Arrange: Set up source and target
    auto source = SceneFlags<TestFlag> {};
    source.SetLocalValue(TestFlag::kVisible, true);
    const auto expected = source; // Copy for comparison

    // Act: Move assign
    const auto target = std::move(source); // NOLINT(performance-move-const-arg)

    // Assert: Target should have expected state
    EXPECT_EQ(target, expected);
}

NOLINT_TEST_F(SceneFlagsBasicTest, GetFlag_ReturnsCompleteStateForFlag)
{
    // Arrange: Set up a flag with complete state
    constexpr auto complete_flag = SceneFlag {}
                                       .SetEffectiveValueBit(true)
                                       .SetInheritedBit(true)
                                       .SetDirtyBit(true)
                                       .SetPreviousValueBit(true);
    flags_.SetFlag(TestFlag::kVisible, complete_flag);

    // Act & Assert: Verify all flag state components using public interface
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_TRUE(flags_.GetPreviousValue(TestFlag::kVisible));
}

NOLINT_TEST_F(SceneFlagsBasicTest, Raw_ReflectsAllStateChanges)
{
    // Arrange: Start with clean flags
    const auto initial_raw = flags_.Raw();

    // Act: Make various changes
    flags_.SetLocalValue(TestFlag::kVisible, true);
    const auto after_set_raw = flags_.Raw();

    flags_.ProcessDirtyFlags();
    const auto after_process_raw = flags_.Raw();

    // Assert: Raw value should change with each state modification
    EXPECT_NE(initial_raw, after_set_raw);
    EXPECT_NE(after_set_raw, after_process_raw);
}

//------------------------------------------------------------------------------
// SceneFlags Inheritance Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlags inheritance functionality.
class SceneFlagsInheritanceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flags container for each test
        flags_ = SceneFlags<TestFlag> {};
    }

    SceneFlags<TestFlag> flags_;
};

NOLINT_TEST_F(SceneFlagsInheritanceTest, BasicInheritanceAndParentUpdate_WorksCorrectly)
{
    // Arrange: Set up flag as inherited from parent
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.ProcessDirtyFlag(TestFlag::kVisible);
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsInherited(TestFlag::kVisible));

    // Act: Enable inheritance for this flag
    flags_.SetInherited(TestFlag::kVisible, true);

    // Act: Update from parent with different value
    UpdateFlagValueFromParent(flags_, TestFlag::kVisible, false);

    // Assert: Flag should be dirty due to parent update
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));

    // Act: Process all dirty flags
    flags_.ProcessDirtyFlags();

    // Assert: Should now inherit parent value
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kVisible));
    EXPECT_FALSE(flags_.GetEffectiveValue(TestFlag::kVisible));
}

NOLINT_TEST_F(SceneFlagsInheritanceTest, InheritedFlagsAdapter_ShowsOnlyInheritedFlags)
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

NOLINT_TEST_F(SceneFlagsInheritanceTest, SetInheritedAll_AllFlagsInheritFromParent)
{
    // Arrange: Set up flags with local values
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
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

NOLINT_TEST_F(SceneFlagsInheritanceTest, SetInheritedAll_VerifyAllFlagsInheritanceState)
{
    // Arrange: Set up flags with mixed inheritance states
    flags_.SetInherited(TestFlag::kVisible, false);
    flags_.SetInherited(TestFlag::kLocked, true);
    flags_.SetInherited(TestFlag::kSelected, false);

    // Act: Set all flags to inherit
    flags_.SetInheritedAll(true);

    // Assert: All flags should now be inherited
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_TRUE(flags_.IsInherited(flag)) << "Flag " << i << " should be inherited";
    }

    // Act: Set all flags to not inherit
    flags_.SetInheritedAll(false);

    // Assert: All flags should now be local
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_FALSE(flags_.IsInherited(flag)) << "Flag " << i << " should not be inherited";
    }
}

NOLINT_TEST_F(SceneFlagsInheritanceTest, UpdateAllInheritFromParent_HandlesComplexParentState)
{
    // Arrange: Set up child flags to inherit
    flags_.SetInheritedAll(true);

    // Create parent with mixed states
    auto parent = SceneFlags<TestFlag> {};
    parent.SetLocalValue(TestFlag::kVisible, true);
    parent.SetLocalValue(TestFlag::kLocked, false);
    parent.SetLocalValue(TestFlag::kSelected, true);
    parent.ProcessDirtyFlags();

    // Act: Update all inherited flags from parent
    flags_.UpdateAllInheritFromParent(parent);

    // Assert: Child should become dirty due to parent updates
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kSelected));

    // Act: Process dirty flags to apply parent values
    flags_.ProcessDirtyFlags();

    // Assert: Child should now have parent's effective values
    EXPECT_EQ(flags_.GetEffectiveValue(TestFlag::kVisible), parent.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_EQ(flags_.GetEffectiveValue(TestFlag::kLocked), parent.GetEffectiveValue(TestFlag::kLocked));
    EXPECT_EQ(flags_.GetEffectiveValue(TestFlag::kSelected), parent.GetEffectiveValue(TestFlag::kSelected));
}

NOLINT_TEST_F(SceneFlagsInheritanceTest, InheritedFlagsRange_EmptyWhenNoFlagsInherited)
{
    // Arrange: Flags with no inheritance (all local)
    flags_.SetInheritedAll(false);

    // Act: Collect inherited flags
    auto inherited_flags = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::inherited_flags(flags_)) {
        inherited_flags.push_back(flag);
    }

    // Assert: Should find no inherited flags
    EXPECT_TRUE(inherited_flags.empty());
}

NOLINT_TEST_F(SceneFlagsInheritanceTest, FlagAccessibility_AllFlagsAccessibleWithInheritance)
{
    // Arrange: Set different states for each flag including inheritance
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetInherited(TestFlag::kLocked, true);
    flags_.SetLocalValue(TestFlag::kSelected, false);

    // Act & Assert: All flags should be accessible with correct states using public interface
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kSelected));
}

NOLINT_TEST_F(SceneFlagsInheritanceTest, RawPreservation_PreservesAllInheritanceStates)
{
    // Arrange: Create flags with complex mixed states including inheritance
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetInherited(TestFlag::kLocked, true);
    flags_.SetLocalValue(TestFlag::kSelected, false);
    flags_.ProcessDirtyFlags();
    flags_.SetInherited(TestFlag::kVisible, true); // Make it inherited after processing

    // Act: Get raw value and set it to a new container
    const auto raw_value = flags_.Raw();
    auto new_flags = SceneFlags<TestFlag> {};
    new_flags.SetRaw(raw_value);

    // Assert: All flag states including inheritance should be preserved exactly
    EXPECT_EQ(new_flags, flags_);
    EXPECT_EQ(new_flags.GetEffectiveValue(TestFlag::kVisible), flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_EQ(new_flags.IsInherited(TestFlag::kVisible), flags_.IsInherited(TestFlag::kVisible));
    EXPECT_EQ(new_flags.IsInherited(TestFlag::kLocked), flags_.IsInherited(TestFlag::kLocked));
}

//------------------------------------------------------------------------------
// SceneFlags Error Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlags error handling.
class SceneFlagsErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flags container for each test
        flags_ = SceneFlags<TestFlag> {};
    }

    SceneFlags<TestFlag> flags_;
};

NOLINT_TEST_F(SceneFlagsErrorTest, OutOfBoundsAccess_DoesNotThrow)
{
    // Arrange: Invalid flag enum value
    constexpr auto bogus = static_cast<TestFlag>(99);

    // Act & Assert: Should not throw (graceful degradation)
    NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.GetEffectiveValue(bogus));
}

NOLINT_TEST_F(SceneFlagsErrorTest, ProcessDirtyFlag_ReturnsFalseWhenFlagNotDirty)
{
    // Arrange: Clean flag that is not dirty
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kVisible));

    // Act: Try to process a non-dirty flag
    const auto result = flags_.ProcessDirtyFlag(TestFlag::kVisible);

    // Assert: Should return false since flag was not dirty
    EXPECT_FALSE(result);
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kVisible)); // Should remain non-dirty
}

//------------------------------------------------------------------------------
// SceneFlags Edge Case Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneFlags edge case handling.
class SceneFlagsEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Arrange: Initialize clean flags container for each test
        flags_ = SceneFlags<TestFlag> {};
    }

    SceneFlags<TestFlag> flags_;
};

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, Iterator_EmptyEnumHandledCorrectly)
{
    // Arrange: Test with enum that has no actual flags
    enum class EmptyTestFlag : uint8_t { kCount };
    static_assert(SceneFlagEnum<EmptyTestFlag>);
    const auto zero_flags = SceneFlags<EmptyTestFlag> {};

    // Act: Iterate over empty enum
    std::size_t count = 0;
    for (const auto item : zero_flags) {
        (void)item;
        count++;
    }

    // Assert: Should handle empty enum gracefully
    EXPECT_EQ(zero_flags.begin(), zero_flags.end());
    EXPECT_EQ(count, 0);
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, DirtyFlagsRange_EmptyWhenNoFlagsDirty)
{
    // Arrange: Clean flags container with no dirty flags
    EXPECT_EQ(flags_.CountDirtyFlags(), 0);

    // Act: Count dirty flags using range
    std::size_t dirty_count = 0;
    for (const auto flag : flags_.dirty_flags()) {
        (void)flag;
        ++dirty_count;
    }

    // Assert: Should find no dirty flags
    EXPECT_EQ(dirty_count, 0);
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, EffectiveTrueFlagsRange_EmptyWhenAllFlagsFalse)
{
    // Arrange: All flags set to false
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, false);
    }
    flags_.ProcessDirtyFlags();

    // Act: Collect flags with true effective values
    auto true_flags = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::effective_true_flags(flags_)) {
        true_flags.push_back(flag);
    }

    // Assert: Should find no true flags
    EXPECT_TRUE(true_flags.empty());
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, EffectiveFalseFlagsRange_EmptyWhenAllFlagsTrue)
{
    // Arrange: All flags set to true
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, true);
    }
    flags_.ProcessDirtyFlags();

    // Act: Collect flags with false effective values
    auto false_flags = std::vector<TestFlag> {};
    for (auto [flag, state] : oxygen::scene::effective_false_flags(flags_)) {
        false_flags.push_back(flag);
    }

    // Assert: Should find no false flags
    EXPECT_TRUE(false_flags.empty());
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, GetFlag_WithAllFlagEnumValues)
{
    // Arrange: Test with every possible TestFlag enum value
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, false);
    flags_.SetLocalValue(TestFlag::kSelected, true);
    flags_.ProcessDirtyFlags(); // Act & Assert: All enum values should be accessible using public interface
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);

        // Act: Access flag state should not throw
        NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.GetEffectiveValue(flag));
        NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.IsDirty(flag));
        NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.IsInherited(flag));
        NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.GetPreviousValue(flag));

        // Assert: Flag state should be consistent (processed flags should not be dirty)
        EXPECT_FALSE(flags_.IsDirty(flag));
    }
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, SetRaw_WithZeroValue)
{
    // Arrange: Set up flags with some values first
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();

    // Act: Set raw to zero (all flags false, no inheritance, no dirty bits)
    flags_.SetRaw(0);

    // Assert: All flags should be in default state
    ExpectAllFlagsEffectiveValue(flags_, false);
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_FALSE(flags_.IsDirty(flag));
        EXPECT_FALSE(flags_.IsInherited(flag));
        EXPECT_FALSE(flags_.GetPreviousValue(flag));
    }
}

NOLINT_TEST_F(SceneFlagsEdgeCaseTest, SetRaw_WithMaximumValidValue)
{
    // Arrange: Calculate maximum valid value for our flag count
    // Each flag uses 5 bits, so for 3 flags we have 15 bits total
    constexpr auto max_valid_value = (1ULL << (static_cast<std::size_t>(TestFlag::kCount) * 5)) - 1;

    // Act: Set raw to maximum valid value (all bits set for all flags)
    flags_.SetRaw(max_valid_value); // Assert: All flags should have all bits set using public interface
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);

        EXPECT_TRUE(flags_.GetEffectiveValue(flag));
        EXPECT_TRUE(flags_.IsInherited(flag));
        EXPECT_TRUE(flags_.IsDirty(flag));
        EXPECT_TRUE(flags_.GetPreviousValue(flag));
    }
}

} // namespace
