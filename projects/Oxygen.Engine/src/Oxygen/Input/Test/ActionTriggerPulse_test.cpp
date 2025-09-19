//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerPulse

#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Duration;
using oxygen::SecondsToDuration;
using oxygen::input::ActionTriggerPulse;
using oxygen::input::ActionValue;

//! Should NOT trigger if not actuated
NOLINT_TEST(ActionTriggerPulse_Edge, NoTriggerWithoutActuation)
{
  ActionTriggerPulse trigger;

  ActionValue v { false };
  trigger.UpdateState(v, SecondsToDuration(0.1F));
  EXPECT_FALSE(trigger.IsTriggered());
}

NOLINT_TEST(ActionTriggerPulse_Basic, TriggersAtIntervalsWhileHeld)
{
  ActionTriggerPulse trigger;
  trigger.SetInterval(0.1F);

  ActionValue v { false };
  v.Update(true);

  // Start -> should NOT trigger immediately, only after interval elapses
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Advance below interval -> no trigger
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Reach first interval -> trigger
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Next interval -> trigger again
  trigger.UpdateState(v, SecondsToDuration(0.1F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Release -> cancel the pulse sequence
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsCanceled());
}

//! Cancel when released after multiple pulses
NOLINT_TEST(ActionTriggerPulse_Edge, CanceledWhenReleasedAfterMultiplePulses)
{
  ActionTriggerPulse trigger;
  trigger.SetInterval(0.05F);

  ActionValue v { false };
  v.Update(true);

  // On start -> no immediate trigger
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Two pulses
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  EXPECT_TRUE(trigger.IsTriggered());
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Release
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsCanceled());
}

//! Slightly late frames should still trigger within jitter tolerance
NOLINT_TEST(ActionTriggerPulse_Stability, JitterToleranceAllowsLateFrame)
{
  ActionTriggerPulse trigger;
  trigger.SetInterval(0.1F);
  trigger.SetJitterTolerance(0.02F);

  ActionValue v { false };
  v.Update(true);

  // Start (no immediate trigger)
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Slightly late frame: 0.11s > 0.1s, but within 0.02s tolerance
  trigger.UpdateState(v, SecondsToDuration(0.11F));
  EXPECT_TRUE(trigger.IsTriggered());
}

//! With phase alignment enabled, overshoot is carried; recurring late frames
//! still produce at most one trigger per update without drifting cadence
NOLINT_TEST(ActionTriggerPulse_Stability, PhaseAlignmentCarriesOvershoot)
{
  ActionTriggerPulse trigger;
  trigger.SetInterval(0.1F);
  trigger.EnablePhaseAlignment(true);

  ActionValue v { false };
  v.Update(true);

  // No immediate trigger
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Repeated slightly late frames should each cause one trigger
  for (int i = 0; i < 3; ++i) {
    trigger.UpdateState(v, SecondsToDuration(0.11F));
    EXPECT_TRUE(trigger.IsTriggered());
  }

  // Release -> canceled
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsCanceled());
}

//! Linear ramping from a slower to a faster interval increases cadence over
//! time
NOLINT_TEST(ActionTriggerPulse_Dynamics, RateRampSpeedsUp)
{
  ActionTriggerPulse trigger;
  // Start at 0.2s interval, ramp to 0.05s over 1s
  trigger.SetRateRamp(0.2F, 0.05F, 1.0F);

  ActionValue v { false };
  v.Update(true);

  // No trigger on start
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_FALSE(trigger.IsTriggered());

  // Early phase: ~0.2s interval
  trigger.UpdateState(v, SecondsToDuration(0.2F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Later phase: interval has reduced; a shorter delay should now be enough
  // Simulate elapsed time toward the end of the ramp; one more long step
  trigger.UpdateState(v, SecondsToDuration(0.5F)); // advance ramp progression
  EXPECT_FALSE(trigger.IsTriggered());

  // Now, a shorter step should reach the (reduced) interval
  trigger.UpdateState(v, SecondsToDuration(0.08F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Release
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsCanceled());
}

} // namespace
