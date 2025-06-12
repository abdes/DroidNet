//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Test/Fixtures/SceneTest.h>

namespace oxygen::scene::testing {

//=============================================================================
// Scene Flags Test Infrastructure
//=============================================================================

/// Enumeration for flag testing scenarios.
enum class TestFlag : std::uint8_t {
  kVisible = 0,
  kStatic = 1,
  kIgnoreParentTransform = 2,
  kCastShadows = 3,
  kReceiveShadows = 4,
  kCount = 5
};

/// Base fixture for SceneFlags testing. Provides common flag testing
/// infrastructure.
template <typename FlagEnum = TestFlag>
class SceneFlagsTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    // Initialize clean flags container for each test
    flags_ = SceneFlags<FlagEnum> {};
  }

  SceneFlags<FlagEnum> flags_;

  //=== Flag Testing Helpers ===----------------------------------------------//

  /// Validates that all flags have the expected effective value.
  static void ExpectAllFlagsEffectiveValue(
    const SceneFlags<FlagEnum>& flags, bool expected_value)
  {
    for (auto flag_enum : flags) {
      const auto flag = flags.GetFlag(flag_enum);
      EXPECT_EQ(flag.GetEffectiveValue(), expected_value)
        << "Flag " << static_cast<int>(flag_enum)
        << " should have effective value " << expected_value;
    }
  }

  /// Validates that a specific flag has the expected effective value.
  static void ExpectFlagEffectiveValue(
    const SceneFlags<FlagEnum>& flags, FlagEnum flag_enum, bool expected_value)
  {
    const auto flag = flags.GetFlag(flag_enum);
    EXPECT_EQ(flag.GetEffectiveValue(), expected_value)
      << "Flag " << static_cast<int>(flag_enum)
      << " should have effective value " << expected_value;
  }

  /// Validates that a specific flag is dirty.
  static void ExpectFlagDirty(
    const SceneFlags<FlagEnum>& flags, FlagEnum flag_enum)
  {
    const auto flag = flags.GetFlag(flag_enum);
    EXPECT_TRUE(flag.IsDirty())
      << "Flag " << static_cast<int>(flag_enum) << " should be dirty";
  }

  /// Validates that a specific flag is not dirty.
  static void ExpectFlagClean(
    const SceneFlags<FlagEnum>& flags, FlagEnum flag_enum)
  {
    const auto flag = flags.GetFlag(flag_enum);
    EXPECT_FALSE(flag.IsDirty())
      << "Flag " << static_cast<int>(flag_enum) << " should be clean";
  }

  /// Validates that all flags are clean (not dirty).
  static void ExpectAllFlagsClean(const SceneFlags<FlagEnum>& flags)
  {
    for (auto flag_enum : flags) {
      ExpectFlagClean(flags, flag_enum);
    }
  }

  /// Validates the number of dirty flags.
  static void ExpectDirtyFlagCount(
    const SceneFlags<FlagEnum>& flags, std::size_t expected_count)
  {
    std::size_t dirty_count = 0;
    for (auto flag_enum : flags.DirtyFlagsRange()) {
      ++dirty_count;
    }
    EXPECT_EQ(dirty_count, expected_count)
      << "Expected " << expected_count << " dirty flags, found " << dirty_count;
  }

  /// Validates the number of flags with effective value true.
  static void ExpectTrueFlagCount(
    const SceneFlags<FlagEnum>& flags, std::size_t expected_count)
  {
    std::size_t true_count = 0;
    for (auto flag_enum : flags.EffectiveTrueFlagsRange()) {
      ++true_count;
    }
    EXPECT_EQ(true_count, expected_count)
      << "Expected " << expected_count << " true flags, found " << true_count;
  }

  /// Validates the number of flags with effective value false.
  static void ExpectFalseFlagCount(
    const SceneFlags<FlagEnum>& flags, std::size_t expected_count)
  {
    std::size_t false_count = 0;
    for (auto flag_enum : flags.EffectiveFalseFlagsRange()) {
      ++false_count;
    }
    EXPECT_EQ(false_count, expected_count)
      << "Expected " << expected_count << " false flags, found " << false_count;
  }

  //=== Flag Manipulation Helpers ===----------------------------------------//

  /// Sets a flag to a specific value and validates the change.
  void SetAndValidateFlag(FlagEnum flag_enum, bool value)
  {
    flags_.SetLocalValue(flag_enum, value);
    ExpectFlagEffectiveValue(flags_, flag_enum, value);
    ExpectFlagDirty(flags_, flag_enum);
  }

  /// Sets multiple flags to specific values.
  void SetMultipleFlags(
    const std::vector<std::pair<FlagEnum, bool>>& flag_values)
  {
    for (const auto& [flag_enum, value] : flag_values) {
      flags_.SetLocalValue(flag_enum, value);
    }
  }

  /// Processes dirty flags and validates they become clean.
  void ProcessAndValidateClean()
  {
    for (auto flag_enum : flags_.DirtyFlagsRange()) {
      const bool processed = flags_.ProcessDirtyFlag(flag_enum);
      EXPECT_TRUE(processed) << "Flag should have been processed";
    }
    ExpectAllFlagsClean(flags_);
  }

  //=== Common Flag Scenarios ===---------------------------------------------//

  /// Creates a flags container with mixed values for testing.
  [[nodiscard]] auto CreateMixedFlags() -> SceneFlags<FlagEnum>
  {
    SceneFlags<FlagEnum> mixed_flags;
    if constexpr (std::is_same_v<FlagEnum, TestFlag>) {
      mixed_flags.SetLocalValue(TestFlag::kVisible, true);
      mixed_flags.SetLocalValue(TestFlag::kStatic, false);
      mixed_flags.SetLocalValue(TestFlag::kIgnoreParentTransform, true);
      mixed_flags.SetLocalValue(TestFlag::kCastShadows, false);
      mixed_flags.SetLocalValue(TestFlag::kReceiveShadows, true);
    }
    return mixed_flags;
  }

  /// Creates a flags container with all flags set to true.
  [[nodiscard]] auto CreateAllTrueFlags() -> SceneFlags<FlagEnum>
  {
    SceneFlags<FlagEnum> all_true_flags;
    for (auto flag_enum : all_true_flags) {
      all_true_flags.SetLocalValue(flag_enum, true);
    }
    return all_true_flags;
  }

  /// Creates a flags container with all flags set to false.
  [[nodiscard]] auto CreateAllFalseFlags() -> SceneFlags<FlagEnum>
  {
    SceneFlags<FlagEnum> all_false_flags;
    for (auto flag_enum : all_false_flags) {
      all_false_flags.SetLocalValue(flag_enum, false);
    }
    return all_false_flags;
  }

  //=== Inheritance Testing Helpers ===---------------------------------------//

  /// Tests flag inheritance from parent to child.
  void TestBasicInheritance()
  {
    // Create parent flags with some values
    auto parent_flags = CreateMixedFlags();
    parent_flags.ProcessDirtyFlags();

    // Create child flags and inherit from parent
    SceneFlags<FlagEnum> child_flags;
    child_flags.UpdateAllInheritFromParent(parent_flags);

    // Validate inheritance
    for (auto flag_enum : parent_flags) {
      const auto parent_flag = parent_flags.GetFlag(flag_enum);
      const auto child_flag = child_flags.GetFlag(flag_enum);
      EXPECT_EQ(child_flag.GetEffectiveValue(), parent_flag.GetEffectiveValue())
        << "Child should inherit parent's effective value for flag "
        << static_cast<int>(flag_enum);
    }
  }
};

//=== Categorized Flag Test Fixtures ===-----------------------------------//

/// Base class for basic flag functionality tests.
template <typename FlagEnum = TestFlag>
class SceneFlagsBasicTest : public SceneFlagsTest<FlagEnum> { };

/// Base class for flag inheritance tests.
template <typename FlagEnum = TestFlag>
class SceneFlagsInheritanceTest : public SceneFlagsTest<FlagEnum> { };

/// Base class for flag error handling tests.
template <typename FlagEnum = TestFlag>
class SceneFlagsErrorTest : public SceneFlagsTest<FlagEnum> { };

/// Base class for flag edge case tests.
template <typename FlagEnum = TestFlag>
class SceneFlagsEdgeCaseTest : public SceneFlagsTest<FlagEnum> { };

/// Base class for atomic flag tests.
template <typename FlagEnum = TestFlag>
class SceneFlagsAtomicTest : public SceneFlagsTest<FlagEnum> { };

/// Convenience aliases for TestFlag-based testing.
using TestSceneFlagsTest = SceneFlagsTest<TestFlag>;
using TestSceneFlagsBasicTest = SceneFlagsBasicTest<TestFlag>;
using TestSceneFlagsInheritanceTest = SceneFlagsInheritanceTest<TestFlag>;
using TestSceneFlagsErrorTest = SceneFlagsErrorTest<TestFlag>;
using TestSceneFlagsEdgeCaseTest = SceneFlagsEdgeCaseTest<TestFlag>;
using TestSceneFlagsAtomicTest = SceneFlagsAtomicTest<TestFlag>;

} // namespace oxygen::scene::testing
