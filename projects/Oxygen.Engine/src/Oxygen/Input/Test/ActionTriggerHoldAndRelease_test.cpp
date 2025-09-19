//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerHoldAndRelease

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Axis1D;
using oxygen::input::ActionTriggerHoldAndRelease;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;
using namespace std::chrono_literals;

namespace {

NOLINT_TEST(ActionTriggerHoldAndRelease, TriggersOnReleaseAfterHold)
{ // NOLINT(*-avoid-c-arrays)
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.2F);

  ActionValue v { false };

  // Press and hold below threshold
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Continue holding over threshold
  trigger.UpdateState(v, CanonicalDuration { 150ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> triggers
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Releasing before the hold threshold should not trigger
NOLINT_TEST(ActionTriggerHoldAndRelease, NoTriggerIfReleasedBeforeThreshold)
{ // NOLINT(*-avoid-c-arrays)
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.2F);

  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional Scenarios for HoldAndRelease Trigger
//===----------------------------------------------------------------------===//

namespace {

//! Fires only on release at the exact boundary (>= threshold)
NOLINT_TEST(ActionTriggerHoldAndRelease, FiresAtExactThresholdOnRelease)
{
  // Arrange
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.20F);
  ActionValue v { false };

  // Act: press and hold exactly the threshold duration
  v.Update(true);
  trigger.UpdateState(v,
    CanonicalDuration { std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(0.20F)) });

  // Assert: still not triggered until release
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> triggers
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Does not trigger before release even after surpassing threshold
NOLINT_TEST(ActionTriggerHoldAndRelease, NoTriggerBeforeRelease)
{
  // Arrange
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.10F);
  ActionValue v { false };

  // Act: press and hold beyond threshold
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v,
    CanonicalDuration { std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(0.05F)) });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v,
    CanonicalDuration { std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(0.10F)) });
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> triggers once
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Axis positive: triggers on release after being above threshold long enough
NOLINT_TEST(ActionTriggerHoldAndRelease, TriggersOnPositiveReleaseAfterHold)
{
  // Arrange
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.05F);
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Rise above threshold and hold
  v.Update(Axis1D { 0.41F });
  trigger.UpdateState(v,
    CanonicalDuration { std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(0.03F)) });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v, CanonicalDuration { 20ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> triggers
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Axis negative: triggers on release after being below -threshold long enough
NOLINT_TEST(ActionTriggerHoldAndRelease, TriggersOnNegativeReleaseAfterHold)
{
  // Arrange
  ActionTriggerHoldAndRelease trigger;
  trigger.SetHoldDurationThreshold(0.05F);
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Go below negative threshold and hold
  v.Update(Axis1D { -0.50F });
  trigger.UpdateState(v, CanonicalDuration { 30ms });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v, CanonicalDuration { 20ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Return to zero (release) -> triggers
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

} // namespace
