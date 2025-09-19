//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerTap

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;
using namespace std::chrono_literals;

namespace {

NOLINT_TEST(ActionTriggerTap, TriggersOnQuickRelease)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.2F);

  ActionValue v { false };

  // Press and quick release
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  EXPECT_TRUE(trigger.IsTriggered());
}

//! Triggers when released exactly at the threshold boundary (<= threshold)
NOLINT_TEST(ActionTriggerTap, FiresAtExactThreshold)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 200ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert
  EXPECT_TRUE(trigger.IsTriggered());
}

//! After too-long hold, release cancels the tap (no Triggered)
NOLINT_TEST(ActionTriggerTap, SetsCanceledOnRelease)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.10F);
  ActionValue v { false };

  // Act: hold longer than threshold then release
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 150ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());
}

//! After cancel, a subsequent quick tap should trigger normally
NOLINT_TEST(ActionTriggerTap, TriggersAfterCancel)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.10F);
  ActionValue v { false };

  // Act 1: cancel
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 200ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert 1
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCanceled());

  // Act 2: quick tap
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert 2
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Two quick taps should trigger twice (no auto-repeat while held)
NOLINT_TEST(ActionTriggerTap, DoubleTapFiresTwice)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act & Assert: first tap
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());

  // Act & Assert: second tap
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 40ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Below actuation threshold, press+release should not trigger
NOLINT_TEST(ActionTriggerTap, NoTriggerBelowActuationThreshold)
{
  // Arrange
  ActionTriggerTap trigger;
  // Force a threshold higher than boolean 'true' mapping (assumed 1.0)
  trigger.SetActuationThreshold(1.1F);
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act: press/release with boolean value; should not actuate
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 50ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Extremely short press+release still within window should trigger
NOLINT_TEST(ActionTriggerTap, VeryShortPressTriggers)
{
  // Arrange
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.20F);
  ActionValue v { false };

  // Act
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert
  EXPECT_TRUE(trigger.IsTriggered());
}

NOLINT_TEST(ActionTriggerTap, DoesNotTriggerOnLongHold)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.1F);

  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 200ms });
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});

  EXPECT_FALSE(trigger.IsTriggered());
}

//! Does not trigger if never released
NOLINT_TEST(ActionTriggerTap, NoTriggerIfNotReleased)
{
  ActionTriggerTap trigger;
  trigger.SetTapTimeThreshold(0.2F);
  ActionValue v { false };
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration { 100ms });
  // No release within test
  EXPECT_FALSE(trigger.IsTriggered());
}

} // namespace
