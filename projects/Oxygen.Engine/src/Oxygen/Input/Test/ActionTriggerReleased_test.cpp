//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerReleased

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Axis1D;
using oxygen::input::ActionTriggerReleased;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;

//! Released triggers on release after being actuated
NOLINT_TEST(ActionTriggerReleased, TriggersOnReleaseAfterPress)
{
  // Arrange
  ActionTriggerReleased trigger;
  ActionValue v { false };

  // Act: press -> no trigger yet
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> triggers once
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());

  // Idle stays non-triggered
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional Edge Cases
//===----------------------------------------------------------------------===//

namespace {

//! Releasing without prior actuation should not trigger
NOLINT_TEST(ActionTriggerReleased, NoTriggerOnReleaseWithoutPriorPress)
{
  ActionTriggerReleased trigger;
  ActionValue v { false };
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsIdle());
}

//! Below actuation threshold: never enters Ongoing, release should not trigger
NOLINT_TEST(ActionTriggerReleased, NoTriggerBelowActuationThreshold)
{
  // Arrange
  ActionTriggerReleased trigger;
  trigger.SetActuationThreshold(1.1F);
  ActionValue v { false };

  // Act: press with boolean 'true' (mapped to 1.0) < 1.1 -> no ongoing
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> should not trigger because never ongoing
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Axis positive: triggers on falling edge after being above threshold
NOLINT_TEST(ActionTriggerReleased, TriggersOnPositiveFall)
{
  // Arrange
  ActionTriggerReleased trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Rise above threshold (Ongoing), no trigger yet
  v.Update(Axis1D { 0.41F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Fall to zero (release) -> trigger
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Axis negative: falling edge from abs(value) > threshold triggers
NOLINT_TEST(ActionTriggerReleased, TriggersOnNegativeRiseToZero)
{
  // Arrange
  ActionTriggerReleased trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Go negative beyond threshold (Ongoing), no trigger yet
  v.Update(Axis1D { -0.50F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Return to zero (release) -> trigger
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Released is instantaneous (no cancellation semantics)
NOLINT_TEST(ActionTriggerReleased, NeverCanceled)
{
  // Arrange
  ActionTriggerReleased trigger;
  ActionValue v { false };

  // Idle -> no cancel
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsCanceled());

  // Press -> Ongoing
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsCanceled());

  // Release -> Triggered, still not canceled
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
  EXPECT_FALSE(trigger.IsCanceled());
}

} // namespace
