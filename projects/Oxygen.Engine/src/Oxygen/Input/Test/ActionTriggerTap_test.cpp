//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerTap

#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Duration;
using oxygen::SecondsToDuration;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValue;

TEST(ActionTriggerTap_Basic, TriggersOnQuickRelease)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.2F);

  ActionValue v { false };

  // Press and quick release
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  EXPECT_TRUE(trigger.IsTriggered());
}

//! Triggers when released exactly at the threshold boundary (<= threshold)
NOLINT_TEST(ActionTriggerTap_Boundary, FiresAtExactThreshold)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.20F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert
  EXPECT_TRUE(trigger.IsTriggered());
}

//! After too-long hold, release cancels the tap (no Triggered)
NOLINT_TEST(ActionTriggerTap_Cancel, SetsCanceledOnRelease)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.10F);
  ActionValue v { false };

  // Act: hold longer than threshold then release
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.15F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());
}

//! After cancel, a subsequent quick tap should trigger normally
NOLINT_TEST(ActionTriggerTap_Rearm, TriggersAfterCancel)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.10F);
  ActionValue v { false };

  // Act 1: cancel
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.20F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert 1
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());

  // Act 2: quick tap
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert 2
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Two quick taps should trigger twice (no auto-repeat while held)
NOLINT_TEST(ActionTriggerTap_Sequence, DoubleTapFiresTwice)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act & Assert: first tap
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsTriggered());

  // Act & Assert: second tap
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.04F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Below actuation threshold, press+release should not trigger
NOLINT_TEST(ActionTriggerTap_Threshold, NoTriggerBelowActuationThreshold)
{
  // Arrange
  ActionTriggerTap trigger;
  // Force a threshold higher than boolean 'true' mapping (assumed 1.0)
  trigger.SetActuationThreshold(1.1F);
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act: press/release with boolean value; should not actuate
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.05F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Extremely short press+release still within window should trigger
NOLINT_TEST(ActionTriggerTap_Sanity, VeryShortPressTriggers)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.0F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  // Assert
  EXPECT_TRUE(trigger.IsTriggered());
}

TEST(ActionTriggerTap_Basic, DoesNotTriggerOnLongHold)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.1F);

  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.2F));
  v.Update(false);
  trigger.UpdateState(v, SecondsToDuration(0.0F));

  EXPECT_FALSE(trigger.IsTriggered());
}

//! Does not trigger if never released
TEST(ActionTriggerTap_Edge, NoTriggerIfNotReleased)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.2F);
  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, SecondsToDuration(0.1F));
  // No release within test
  EXPECT_FALSE(trigger.IsTriggered());
}

} // namespace
