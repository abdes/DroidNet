//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerPressed

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Axis1D;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionValue;
using oxygen::time::CanonicalDuration;

//! Pressed fires once when actuation crosses threshold and won't retrigger
//! until released and actuated again.
NOLINT_TEST(ActionTriggerPressed, FiresOnceOnActuation)
{
  // Arrange
  ActionTriggerPressed trigger;
  ActionValue v { false };

  // Act & Assert
  // Not actuated -> no trigger
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross threshold -> trigger once
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());

  // Held -> no re-trigger
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Release -> reset internal depletion
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Actuate again -> trigger again
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Below actuation threshold, press should not trigger
NOLINT_TEST(ActionTriggerPressed, NoTriggerBelowActuationThreshold)
{
  // Arrange
  ActionTriggerPressed trigger;
  // Force a threshold higher than boolean 'true' mapping (1.0)
  trigger.SetActuationThreshold(1.1F);
  ActionValue v { false };

  // Act: press with boolean value; should not actuate
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});

  // Assert
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Axis inputs: triggers only when value crosses threshold and not while held
NOLINT_TEST(ActionTriggerPressed, TriggersOnlyAboveThreshold)
{
  // Arrange
  ActionTriggerPressed trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Act & Assert: below threshold -> no trigger
  v.Update(Axis1D { 0.39F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross threshold -> trigger once
  v.Update(Axis1D { 0.41F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());

  // Held above threshold -> no re-trigger
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Release
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});

  // Re-press above threshold -> trigger again
  v.Update(Axis1D { 0.50F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

//! Pressed is instantaneous (no cancellation semantics)
NOLINT_TEST(ActionTriggerPressed, NeverCanceled)
{
  // Arrange
  ActionTriggerPressed trigger;
  ActionValue v { false };

  // Act: idle -> no trigger, no cancel
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_FALSE(trigger.IsCanceled());

  // Press -> trigger; still no cancel
  v.Update(true);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
  EXPECT_FALSE(trigger.IsCanceled());

  // Release -> still no cancel for Pressed
  v.Update(false);
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_FALSE(trigger.IsCanceled());
}

//! Axis inputs: negative values trigger via absolute value check
NOLINT_TEST(ActionTriggerPressed, NegativeCrossesThreshold)
{
  // Arrange
  ActionTriggerPressed trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Act & Assert: negative below threshold -> no trigger
  v.Update(Axis1D { -0.39F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Cross negative threshold (abs > 0.40) -> trigger once
  v.Update(Axis1D { -0.41F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());

  // Held negative -> no re-trigger
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Release
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_FALSE(trigger.IsTriggered());

  // Re-press negative beyond threshold -> trigger again
  v.Update(Axis1D { -0.50F });
  trigger.UpdateState(v, CanonicalDuration {});
  EXPECT_TRUE(trigger.IsTriggered());
}

} // namespace
