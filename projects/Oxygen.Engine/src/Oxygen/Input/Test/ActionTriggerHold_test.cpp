//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerHold

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Axis1D;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;
using namespace std::chrono_literals;

namespace {

NOLINT_TEST(ActionTriggerHold, TriggersAfterThreshold)
{
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.1F);
  trigger.OneShot(true);

  ActionValue v { false };

  // Press and accumulate time
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross threshold
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_TRUE(trigger.IsTriggered());

  // Held more -> one-shot, no further triggers
  trigger.UpdateState(v, CanonicalDuration { 500ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> completed
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsCompleted());
}

//! Does not trigger if released before threshold
NOLINT_TEST(ActionTriggerHold, NoTriggerIfReleasedBeforeThreshold)
{
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.2F);
  trigger.OneShot(true);

  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());
}

//! Fires when held exactly at the threshold boundary (>= threshold)
NOLINT_TEST(ActionTriggerHold, FiresAtExactThreshold)
{
  // Arrange
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.20F);
  trigger.OneShot(true);
  ActionValue v { false };

  // Act: press and hold exactly the threshold duration
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 200ms });

  // Assert
  EXPECT_TRUE(trigger.IsTriggered());

  // Release -> completed
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsCompleted());
}

//! Axis inputs: triggers only when abs(value) held long enough
NOLINT_TEST(ActionTriggerHold, TriggersOnAxisAboveThreshold)
{
  // Arrange
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.10F);
  trigger.OneShot(true);
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Below threshold -> accumulate but no ongoing state; ensure no trigger
  v.Update(Axis1D { 0.39F });
  trigger.UpdateState(v, CanonicalDuration { 200ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross positive threshold and hold
  v.Update(Axis1D { 0.41F });
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_TRUE(trigger.IsTriggered());

  // Release -> completed
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsCompleted());

  // Negative side: cross threshold and hold
  v.Update(Axis1D { -0.50F });
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  EXPECT_TRUE(trigger.IsTriggered());
}

//! When OneShot(false), Hold can retrigger while held (every update >=
//! threshold)
NOLINT_TEST(ActionTriggerHold, RepeatsIfOneShotDisabled)
{
  // Arrange
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.05F);
  trigger.OneShot(false);
  ActionValue v { false };

  // Act: press and hold; first crossing -> trigger
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_TRUE(trigger.IsTriggered());

  // Still held and beyond threshold -> triggers again since OneShot is false
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  EXPECT_TRUE(trigger.IsTriggered());

  // Release -> no more triggers, not completed (since not part of completed
  // semantics), but IsIdle
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsIdle());
}

//! OneShot(true): no extra triggers while continuously held beyond first fire
NOLINT_TEST(ActionTriggerHold, NoExtraTriggersWhileHeld)
{
  // Arrange
  ActionTriggerHold trigger;
  trigger.SetHoldDurationThreshold(0.05F);
  trigger.OneShot(true);
  ActionValue v { false };

  // Act: press and hold; just below threshold -> no trigger
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 40ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross threshold -> trigger once
  trigger.UpdateState(v, CanonicalDuration { 10ms });
  EXPECT_TRUE(trigger.IsTriggered());

  // Keep holding well beyond threshold -> no further triggers
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  EXPECT_FALSE(trigger.IsTriggered());
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> completed
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsCompleted());
}

} // namespace
