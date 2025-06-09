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
// SceneFlags Atomic Tests
//------------------------------------------------------------------------------

//! Test fixture for AtomicSceneFlags thread-safe container.
class SceneFlagsAtomicTest : public testing::Test {
protected:
    AtomicSceneFlags<TestFlag> atomic_flags_ {};
};

NOLINT_TEST_F(SceneFlagsAtomicTest, StoreAndLoad_PreservesState)
{
    // Arrange: Create flags with specific state
    auto flags = SceneFlags<TestFlag> {};
    flags.SetLocalValue(TestFlag::kVisible, true);
    flags.ProcessDirtyFlags();

    // Act: Store flags atomically
    atomic_flags_.Store(flags);

    // Act: Load flags atomically
    const auto loaded = atomic_flags_.Load();

    // Assert: Loaded flags should match stored flags
    EXPECT_TRUE(loaded.GetEffectiveValue(TestFlag::kVisible));
}

NOLINT_TEST_F(SceneFlagsAtomicTest, Exchange_ReturnsOldValueAndSetsNew)
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
    const auto returned_flags = atomic_flags_.Exchange(new_flags);

    // Assert: Should return old value and store new value
    EXPECT_TRUE(returned_flags.GetEffectiveValue(TestFlag::kVisible));
    EXPECT_EQ(atomic_flags_.Load(), new_flags);
}

NOLINT_TEST_F(
    SceneFlagsAtomicTest, CompareExchangeWeak_SucceedsWithCorrectExpected)
{
    // Arrange: Set up expected and desired states
    auto expected = SceneFlags<TestFlag> {};
    auto desired = SceneFlags<TestFlag> {};
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    // Act: Attempt weak compare-exchange with correct expected value
    const auto result = atomic_flags_.CompareExchangeWeak(expected, desired);

    // Assert: Should succeed and update to desired value
    EXPECT_TRUE(result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

NOLINT_TEST_F(
    SceneFlagsAtomicTest, CompareExchangeStrong_SucceedsWithCorrectExpected)
{
    // Arrange: Set up expected and desired states
    auto expected = SceneFlags<TestFlag> {};
    auto desired = SceneFlags<TestFlag> {};
    expected.SetLocalValue(TestFlag::kVisible, true);
    desired.SetLocalValue(TestFlag::kLocked, true);
    atomic_flags_.Store(expected);

    // Act: Attempt strong compare-exchange with correct expected value
    const auto result = atomic_flags_.CompareExchangeStrong(expected, desired);

    // Assert: Should succeed and update to desired value
    EXPECT_TRUE(result);
    EXPECT_EQ(atomic_flags_.Load(), desired);
}

NOLINT_TEST_F(SceneFlagsAtomicTest, CompareExchangeWeak_FailsAndUpdatesExpected)
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
    const auto result
        = atomic_flags_.CompareExchangeWeak(wrong_expected, desired);

    // Assert: Should fail, update expected, and leave current unchanged
    EXPECT_FALSE(result);
    EXPECT_EQ(wrong_expected, current_value);
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

NOLINT_TEST_F(
    SceneFlagsAtomicTest, CompareExchangeStrong_FailsAndUpdatesExpected)
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
    const auto result
        = atomic_flags_.CompareExchangeStrong(wrong_expected, desired);

    // Assert: Should fail, update expected, and leave current unchanged
    EXPECT_FALSE(result);
    EXPECT_EQ(wrong_expected, current_value);
    EXPECT_EQ(atomic_flags_.Load(), current_value);
}

} // namespace
