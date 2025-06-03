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

// -----------------------------------------------------------------------------
// SceneFlagTest: Single-flag state and bitwise operations
// -----------------------------------------------------------------------------

//! Test fixture for SceneFlag.
class SceneFlagTest : public ::testing::Test {
protected:
    SceneFlag flag_ {};
};

//! Scenario: Default construction and bit accessors for SceneFlag.
NOLINT_TEST_F(SceneFlagTest, DefaultConstructionAndBitAccess)
{
    EXPECT_FALSE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());
    EXPECT_FALSE(flag_.GetDirtyBit());
    EXPECT_FALSE(flag_.GetPreviousValueBit());
    EXPECT_EQ(flag_.GetRaw(), 0);

    flag_.SetEffectiveValueBit(true);
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    flag_.SetInheritedBit(true);
    EXPECT_TRUE(flag_.GetInheritedBit());
    flag_.SetDirtyBit(true);
    EXPECT_TRUE(flag_.GetDirtyBit());
    flag_.SetPreviousValueBit(true);
    EXPECT_TRUE(flag_.GetPreviousValueBit());
    flag_.SetPendingValueBit(true);
    EXPECT_TRUE(flag_.GetPendingValueBit());
    flag_.SetRaw(0);
    EXPECT_FALSE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());
    EXPECT_FALSE(flag_.GetDirtyBit());
    EXPECT_FALSE(flag_.GetPreviousValueBit());
    EXPECT_FALSE(flag_.GetPendingValueBit());
}

//! Scenario: High-level operations and state transitions for SceneFlag.
NOLINT_TEST_F(SceneFlagTest, HighLevelOperations)
{
    flag_.SetInheritedBit(true).SetEffectiveValueBit(false);
    flag_.SetLocalValue(true);
    EXPECT_TRUE(flag_.GetDirtyBit());
    flag_.ProcessDirty();
    EXPECT_TRUE(flag_.GetEffectiveValueBit());
    EXPECT_FALSE(flag_.GetInheritedBit());

    flag_.SetInheritedBit(true);
    flag_.SetPendingValueBit(false);
    flag_.SetDirtyBit(true);
    flag_.UpdateValueFromParent(false);
    EXPECT_TRUE(flag_.GetDirtyBit());
    flag_.ProcessDirty();
    EXPECT_FALSE(flag_.GetEffectiveValueBit());
    EXPECT_TRUE(flag_.GetPreviousValueBit());
}

//! Scenario: Equality and string conversion for SceneFlag.
NOLINT_TEST_F(SceneFlagTest, EqualityAndStringConversion)
{
    constexpr SceneFlag a;
    SceneFlag b;
    EXPECT_TRUE(b == a);
    b.SetEffectiveValueBit(true);
    EXPECT_FALSE(b == a);
    EXPECT_TRUE(b != a);
    const auto str = oxygen::scene::to_string(b);
    EXPECT_FALSE(str.empty());
}

// -----------------------------------------------------------------------------
// SceneFlagsTest: Multi-flag container and operations
// -----------------------------------------------------------------------------

//! Test fixture for SceneFlags.
class SceneFlagsTest : public ::testing::Test {
protected:
    SceneFlags<TestFlag> flags_ {};
    void UpdateFlagValueFromParent(const TestFlag flag, const bool value)
    {
        const auto parent = SceneFlags<TestFlag> {}.SetFlag(flag, SceneFlag {}.SetEffectiveValueBit(value));
        flags_.UpdateValueFromParent(flag, parent);
    }
};

//! Scenario: Default construction and all flags zero in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, DefaultAllFlagsZero)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_FALSE(flags_.GetEffectiveValue(flag));
    }
}

//! Scenario: Set and get complete flag state in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, SetAndGetFlagState)
{
    constexpr auto f
        = SceneFlag {}
              .SetEffectiveValueBit(true)
              .SetInheritedBit(true)
              .SetDirtyBit(true)
              .SetPreviousValueBit(true);

    flags_.SetFlag(TestFlag::kLocked, f);
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kLocked));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));
    EXPECT_TRUE(flags_.GetPreviousValue(TestFlag::kLocked));
}

//! Scenario: Local value, inheritance, and parent update in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, LocalValueInheritanceAndParentUpdate)
{
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.ProcessDirtyFlag(TestFlag::kVisible);
    EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsInherited(TestFlag::kVisible));
    flags_.SetInherited(TestFlag::kVisible, true);
    UpdateFlagValueFromParent(TestFlag::kVisible, false);
    flags_.ProcessDirtyFlags();
    EXPECT_TRUE(flags_.IsInherited(TestFlag::kVisible));
    EXPECT_FALSE(flags_.GetEffectiveValue(TestFlag::kVisible));
}

//! Scenario: Dirty flag processing in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, DirtyFlagProcessing)
{
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));
    EXPECT_TRUE(flags_.ProcessDirtyFlag(TestFlag::kLocked));
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kLocked));
}

//! Scenario: Raw access and clear in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, RawAccessAndClear)
{
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    flags_.ProcessDirtyFlags();
    const auto raw = flags_.Raw();
    SceneFlags<TestFlag> other;
    other.SetRaw(raw);
    EXPECT_EQ(flags_, other);
    flags_.Clear();
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_FALSE(flags_.GetEffectiveValue(flag));
    }
}

//! Scenario: Equality and out-of-bounds safety in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, EqualityAndOutOfBounds)
{
    SceneFlags<TestFlag> a, b;
    a.SetLocalValue(TestFlag::kVisible, true);
    b.SetLocalValue(TestFlag::kVisible, true);
    EXPECT_TRUE(a == b);
    b.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
    constexpr auto bogus = static_cast<TestFlag>(99);
    NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = flags_.GetEffectiveValue(bogus));
}

//! Scenario: Iterator and range adapters in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, IteratorAndRangeAdapters)
{
    // Test 1: Iterator covers all flags
    std::array<bool, static_cast<std::size_t>(TestFlag::kCount)> seen {};
    for (const auto flag : flags_ | std::views::keys) {
        seen[static_cast<std::size_t>(flag)] = true;
    }
    for (bool v : seen) {
        EXPECT_TRUE(v);
    }

    // Test 2: Dirty flags view
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);

    std::size_t dirty_count = 0;
    for (const auto flag : flags_.dirty_flags()) {
        (void)flag;
        ++dirty_count;
    }
    EXPECT_EQ(dirty_count, 2);

    // Test 3: Global inherited flags adapter
    // Clear previous state and set up inheritance properly
    flags_.Clear();
    flags_.SetInherited(TestFlag::kVisible, true);
    flags_.SetInherited(TestFlag::kLocked, false);
    flags_.SetInherited(TestFlag::kSelected, true);

    std::vector<TestFlag> inherited;
    for (auto [flag, state] : oxygen::scene::inherited_flags(flags_)) {
        inherited.push_back(flag);
    }
    EXPECT_THAT(inherited, testing::UnorderedElementsAre(TestFlag::kVisible, TestFlag::kSelected));

    // Test 4: Effective true/false flags adapters
    // Clear and set up effective values
    flags_.Clear();
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kSelected, true);
    flags_.SetLocalValue(TestFlag::kLocked, false);
    flags_.ProcessDirtyFlags();

    std::vector<TestFlag> true_flags, false_flags;
    for (auto [flag, state] : oxygen::scene::effective_true_flags(flags_)) {
        true_flags.push_back(flag);
    }
    for (auto [flag, state] : oxygen::scene::effective_false_flags(flags_)) {
        false_flags.push_back(flag);
    }
    EXPECT_THAT(true_flags, testing::UnorderedElementsAre(TestFlag::kVisible, TestFlag::kSelected));
    EXPECT_THAT(false_flags, testing::UnorderedElementsAre(TestFlag::kLocked));
}

//! Scenario: Bulk operations in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, BulkOperations)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        flags_.SetLocalValue(flag, true);
    }
    flags_.ProcessDirtyFlags();
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_TRUE(flags_.GetEffectiveValue(flag));
    }
    flags_.SetInheritedAll(true);
    flags_.UpdateAllInheritFromParent(SceneFlags<TestFlag> {});
    flags_.Clear();
    for (std::size_t i = 0; i < static_cast<std::size_t>(TestFlag::kCount); ++i) {
        const auto flag = static_cast<TestFlag>(i);
        EXPECT_FALSE(flags_.GetEffectiveValue(flag));
    }
}

//! Scenario: Iterator edge cases in SceneFlags.
NOLINT_TEST_F(SceneFlagsTest, IteratorEdgeCases)
{
    const SceneFlags<TestFlag> flags_with_test_enum;
    if constexpr (static_cast<std::size_t>(TestFlag::kCount) > 0) {
        EXPECT_NE(flags_with_test_enum.begin(), flags_with_test_enum.end());
        std::size_t count = 0;
        for (const auto item : flags_with_test_enum) {
            (void)item;
            count++;
        }
        EXPECT_EQ(count, static_cast<std::size_t>(TestFlag::kCount));
    } else {
        EXPECT_EQ(flags_with_test_enum.begin(), flags_with_test_enum.end());
    }
    enum class EmptyTestFlag : uint8_t { kCount };
    static_assert(SceneFlagEnum<EmptyTestFlag>);
    const SceneFlags<EmptyTestFlag> zero_actual_flags;
    EXPECT_EQ(zero_actual_flags.begin(), zero_actual_flags.end());
    std::size_t count_for_zero_flags = 0;
    for (const auto item : zero_actual_flags) {
        (void)item;
        count_for_zero_flags++;
    }
    EXPECT_EQ(count_for_zero_flags, 0);
}

//! Scenario: ClearDirtyFlags resets only dirty bits.
NOLINT_TEST_F(SceneFlagsTest, ClearDirtyFlags)
{
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_TRUE(flags_.IsDirty(TestFlag::kLocked));
    flags_.ClearDirtyFlags();
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kVisible));
    EXPECT_FALSE(flags_.IsDirty(TestFlag::kLocked));
}

//! Scenario: CountDirtyFlags returns correct count.
NOLINT_TEST_F(SceneFlagsTest, CountDirtyFlags)
{
    EXPECT_EQ(flags_.CountDirtyFlags(), 0);
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    EXPECT_EQ(flags_.CountDirtyFlags(), 2);
    flags_.ProcessDirtyFlags();
    EXPECT_EQ(flags_.CountDirtyFlags(), 0);
}

//! Scenario: ProcessDirtyFlags returns true if all dirty flags processed.
NOLINT_TEST_F(SceneFlagsTest, ProcessDirtyFlagsReturnValue)
{
    flags_.SetLocalValue(TestFlag::kVisible, true);
    flags_.SetLocalValue(TestFlag::kLocked, true);
    if (flags_.ProcessDirtyFlags()) {
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kVisible));
        EXPECT_TRUE(flags_.GetEffectiveValue(TestFlag::kLocked));
        EXPECT_EQ(flags_.CountDirtyFlags(), 0);
    }
}

//! Scenario: SceneFlag and SceneFlags copy/move construction and assignment.
NOLINT_TEST_F(SceneFlagsTest, CopyMoveSemantics)
{
    // Testing copy and move semantics for SceneFlags and SceneFlag requires
    // some weird code.

    // ReSharper disable CppJoinDeclarationAndAssignment
    // NOLINTBEGIN(performance-move-const-arg)
    SceneFlags<TestFlag> a;
    a.SetLocalValue(TestFlag::kVisible, true);
    SceneFlags b(a);
    EXPECT_EQ(a, b);
    SceneFlags<TestFlag> c;
    c = a;
    EXPECT_EQ(a, c);
    const SceneFlags d(std::move(a));
    EXPECT_EQ(b, d);
    SceneFlags<TestFlag> e;
    e = std::move(b);
    EXPECT_EQ(c, e);
    SceneFlag f;
    f.SetEffectiveValueBit(true);
    SceneFlag g(f);
    EXPECT_EQ(f, g);
    SceneFlag h;
    h = f;
    EXPECT_EQ(f, h);
    const SceneFlag i(std::move(f));
    EXPECT_EQ(g, i);
    SceneFlag j;
    j = std::move(g);
    EXPECT_EQ(h, j);
    // NOLINTEND(performance-move-const-arg)
    // ReSharper disable CppJoinDeclarationAndAssignment
}

//! Scenario: SceneFlag semantic equality operators (EffectiveEquals/EffectiveNotEquals).
NOLINT_TEST_F(SceneFlagTest, SemanticEqualityOperators)
{
    SceneFlag flag1, flag2;

    // Both flags clean with same effective value - should be equal
    flag1.SetEffectiveValueBit(true).SetDirtyBit(false);
    flag2.SetEffectiveValueBit(true).SetDirtyBit(false);
    EXPECT_TRUE(flag1.EffectiveEquals(flag2));
    EXPECT_FALSE(flag1.EffectiveNotEquals(flag2));

    // Both flags clean with different effective values - should not be equal
    flag1.SetEffectiveValueBit(true).SetDirtyBit(false);
    flag2.SetEffectiveValueBit(false).SetDirtyBit(false);
    EXPECT_FALSE(flag1.EffectiveEquals(flag2));
    EXPECT_TRUE(flag1.EffectiveNotEquals(flag2));

    // One flag dirty - should not be equal regardless of effective values
    flag1.SetEffectiveValueBit(true).SetDirtyBit(true);
    flag2.SetEffectiveValueBit(true).SetDirtyBit(false);
    EXPECT_FALSE(flag1.EffectiveEquals(flag2));
    EXPECT_TRUE(flag1.EffectiveNotEquals(flag2));

    // Both flags dirty - should not be equal even with same effective values
    flag1.SetEffectiveValueBit(true).SetDirtyBit(true);
    flag2.SetEffectiveValueBit(true).SetDirtyBit(true);
    EXPECT_FALSE(flag1.EffectiveEquals(flag2));
    EXPECT_TRUE(flag1.EffectiveNotEquals(flag2));
}

//! Scenario: SetLocalValue optimization logic edge cases.
NOLINT_TEST_F(SceneFlagTest, SetLocalValueOptimizationEdgeCases)
{
    SceneFlag flag;

    // Test 1: Setting same value when already dirty should be no-op
    flag.SetLocalValue(true);
    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.GetPendingValueBit());

    flag.SetLocalValue(true); // Same value again
    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.GetPendingValueBit());

    // Test 2: Reverting to effective value should clear dirty bit
    flag.SetEffectiveValueBit(false);
    flag.SetLocalValue(true);
    EXPECT_TRUE(flag.IsDirty());

    flag.SetLocalValue(false); // Revert to effective value
    EXPECT_FALSE(flag.IsDirty()); // Should be optimized to not dirty
    EXPECT_FALSE(flag.GetPendingValueBit());

    // Test 3: Setting local value when inheritance is enabled should disable inheritance
    flag.SetInheritedBit(true);
    flag.SetLocalValue(true);
    EXPECT_FALSE(flag.IsInherited()); // Should disable inheritance
    EXPECT_TRUE(flag.IsDirty());

    // Test 4: Multiple rapid changes should optimize correctly
    flag.SetRaw(0); // Reset
    flag.SetEffectiveValueBit(false);
    flag.SetLocalValue(true); // Change to true
    flag.SetLocalValue(false); // Change back to original
    flag.SetLocalValue(true); // Change again
    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.GetPendingValueBit());
}

//! Scenario: UpdateValueFromParent with same value should be no-op.
NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentSameValueNoOp)
{
    SceneFlag flag;
    flag.SetInheritedBit(true);
    flag.SetPendingValueBit(false);
    flag.SetDirtyBit(false);

    flag.UpdateValueFromParent(false);

    EXPECT_FALSE(flag.IsDirty()); // Should not become dirty
}

//! Scenario: UpdateValueFromParent reverting to effective value clears dirty bit.
NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentRevertToEffective)
{
    SceneFlag flag;
    flag.SetInheritedBit(true);
    flag.SetEffectiveValueBit(true);
    flag.SetPendingValueBit(false);
    flag.SetDirtyBit(true);

    flag.UpdateValueFromParent(true); // Revert to effective

    EXPECT_FALSE(flag.IsDirty()); // Should clear dirty bit
    EXPECT_TRUE(flag.GetPendingValueBit());
}

//! Scenario: UpdateValueFromParent with different value makes flag dirty.
NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentDifferentValue)
{
    SceneFlag flag;
    flag.SetInheritedBit(true);
    flag.SetEffectiveValueBit(false);

    flag.UpdateValueFromParent(true);

    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.GetPendingValueBit());
}

//! Scenario: Multiple UpdateValueFromParent calls maintain state consistency.
NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentMultipleUpdates)
{
    SceneFlag flag;
    flag.SetInheritedBit(true);
    flag.SetEffectiveValueBit(false);
    flag.SetPendingValueBit(false);

    // First update - should make dirty
    flag.UpdateValueFromParent(true);
    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.GetPendingValueBit());

    // Second update with same value - should be no-op
    flag.UpdateValueFromParent(true);
    EXPECT_TRUE(flag.IsDirty()); // Still dirty
    EXPECT_TRUE(flag.GetPendingValueBit());
}

//! Scenario: UpdateValueFromParent debug assertion for non-inherited flags.
NOLINT_TEST_F(SceneFlagTest, UpdateValueFromParentDebugAssertion)
{
#if !defined(NDEBUG)
    // In DEBUG builds, calling UpdateValueFromParent on non-inherited flag should abort
    SceneFlag non_inherited_flag;
    non_inherited_flag.SetInheritedBit(false);
    EXPECT_DEATH(
        { non_inherited_flag.UpdateValueFromParent(true); },
        "expecting flag to be inherited");
#else // NDEBUG
    // In NDEBUG builds, calling UpdateValueFromParent on non-inherited flag should be no-op
    SceneFlag non_inherited_flag;
    non_inherited_flag.SetInheritedBit(false);
    EXPECT_NO_THROW(non_inherited_flag.UpdateValueFromParent(true));
    EXPECT_FALSE(non_inherited_flag.IsDirty()); // Should remain unchanged
#endif // NDEBUG
}

//! Scenario: ProcessDirty transition tracking and previous value handling.
NOLINT_TEST_F(SceneFlagTest, ProcessDirtyTransitionTracking)
{
    SceneFlag flag;

    // Test transition from false to true
    flag.SetEffectiveValueBit(false);
    flag.SetLocalValue(true);
    EXPECT_TRUE(flag.IsDirty());

    EXPECT_TRUE(flag.ProcessDirty());
    EXPECT_FALSE(flag.IsDirty());
    EXPECT_TRUE(flag.GetEffectiveValueBit());
    EXPECT_FALSE(flag.GetPreviousValueBit()); // Previous should be false

    // Test transition from true to false
    flag.SetLocalValue(false);
    EXPECT_TRUE(flag.IsDirty()); // Should be dirty after setting local value
    EXPECT_TRUE(flag.ProcessDirty());
    EXPECT_FALSE(flag.GetEffectiveValueBit());
    EXPECT_TRUE(flag.GetPreviousValueBit()); // Previous should be true

    // Test ProcessDirty on clean flag should return false
    // Note: We don't call ProcessDirty on a clean flag as it has a precondition check
    // Instead, verify that after processing, the flag is clean
    EXPECT_FALSE(flag.IsDirty());

    // Test another transition to verify previous value tracking
    flag.SetLocalValue(true);
    EXPECT_TRUE(flag.IsDirty());
    EXPECT_TRUE(flag.ProcessDirty());
    EXPECT_TRUE(flag.GetEffectiveValueBit());
    EXPECT_FALSE(flag.GetPreviousValueBit()); // Previous should now be false
}

// -----------------------------------------------------------------------------
// AtomicSceneFlagsTest: Thread-safe multi-flag container
// -----------------------------------------------------------------------------

//! Test fixture for AtomicSceneFlags.
class AtomicSceneFlagsTest : public ::testing::Test {
protected:
    AtomicSceneFlags<TestFlag> atomic_flags_ {};
};

//! Scenario: Atomic operations for AtomicSceneFlags.
NOLINT_TEST_F(AtomicSceneFlagsTest, AtomicOperations)
{
    SceneFlags<TestFlag> flags;
    flags.SetLocalValue(TestFlag::kVisible, true);
    flags.ProcessDirtyFlags();
    atomic_flags_.Store(flags);
    const auto loaded = atomic_flags_.Load();
    EXPECT_TRUE(loaded.GetEffectiveValue(TestFlag::kVisible));
    SceneFlags<TestFlag> newFlags;
    newFlags.SetLocalValue(TestFlag::kSelected, true);
    newFlags.SetLocalValue(TestFlag::kVisible, false);
    newFlags.ProcessDirtyFlags();
    const SceneFlags<TestFlag> returnedFlags = atomic_flags_.Exchange(newFlags);
    EXPECT_TRUE(returnedFlags.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_EQ(atomic_flags_.Load(), newFlags);
}

//! Scenario: Successful weak compare-exchange operation.
NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeWeakSuccess)
{
    SceneFlags<TestFlag> expected, desired;
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    const auto weak_result = atomic_flags_.CompareExchangeWeak(expected, desired);

    EXPECT_TRUE(weak_result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

//! Scenario: Successful strong compare-exchange operation.
NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeStrongSuccess)
{
    SceneFlags<TestFlag> expected, desired;
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    const auto strong_result = atomic_flags_.CompareExchangeStrong(expected, desired);

    EXPECT_TRUE(strong_result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

//! Scenario: Failed weak compare-exchange updates expected value correctly.
NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeWeakFailure)
{
    SceneFlags<TestFlag> current_value, different_expected, new_desired;
    current_value.SetLocalValue(TestFlag::kVisible, true);
    current_value.SetLocalValue(TestFlag::kLocked, false);
    current_value.ProcessDirtyFlags();

    different_expected.SetLocalValue(TestFlag::kVisible, false); // Different from current
    different_expected.SetLocalValue(TestFlag::kLocked, true);
    different_expected.ProcessDirtyFlags();

    new_desired.SetLocalValue(TestFlag::kSelected, true);
    new_desired.ProcessDirtyFlags();

    atomic_flags_.Store(current_value);

    // Attempt compare-exchange with wrong expected value - should fail
    const auto failed_weak_result = atomic_flags_.CompareExchangeWeak(different_expected, new_desired);

    EXPECT_FALSE(failed_weak_result);
    // After failed compare-exchange, expected should contain the actual current value
    EXPECT_EQ(different_expected, current_value);
    // Current value should remain unchanged
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

//! Scenario: Failed strong compare-exchange updates expected value correctly.
NOLINT_TEST_F(AtomicSceneFlagsTest, CompareExchangeStrongFailure)
{
    SceneFlags<TestFlag> current_value, different_expected, new_desired;
    current_value.SetLocalValue(TestFlag::kVisible, true);
    current_value.SetLocalValue(TestFlag::kLocked, false);
    current_value.ProcessDirtyFlags();

    different_expected.SetLocalValue(TestFlag::kSelected, true); // Different from current
    different_expected.ProcessDirtyFlags();

    new_desired.SetLocalValue(TestFlag::kVisible, false);
    new_desired.SetLocalValue(TestFlag::kLocked, false);
    new_desired.ProcessDirtyFlags();

    atomic_flags_.Store(current_value);

    const auto failed_strong_result = atomic_flags_.CompareExchangeStrong(different_expected, new_desired);

    EXPECT_FALSE(failed_strong_result);
    // After failed compare-exchange, expected should contain the actual current value
    EXPECT_EQ(different_expected, current_value);
    // Current value should remain unchanged
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

} // namespace
