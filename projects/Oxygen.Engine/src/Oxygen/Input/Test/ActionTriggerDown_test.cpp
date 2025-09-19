//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerDown

#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Axis1D;
using oxygen::Duration;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionValue;

NOLINT_TEST(ActionTriggerDown_Basic, TriggersWhileHeld)
{
  ActionTriggerDown trigger;
  ActionValue v { false };

  // Not actuated
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());

  // Press -> triggers
  v.Update(true);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());

  // Held -> may trigger again depending on implementation; at least ongoing
  trigger.UpdateState(v, Duration::zero());
  // Not asserting repeated trigger; ensure not canceled
  EXPECT_FALSE(trigger.IsCanceled());

  // Release -> goes idle; completed if triggered once
  v.Update(false);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCompleted());
}

//! Quick press-release still counts as a completed action after at least one
//! trigger
NOLINT_TEST(ActionTriggerDown_Edge, QuickPressReleaseCompletesIfTriggeredOnce)
{
  ActionTriggerDown trigger;
  ActionValue v { false };

  // Quick press -> should trigger once
  v.Update(true);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());

  // Quick release -> completed
  v.Update(false);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsCompleted());
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional Scenarios for Down Trigger
//===----------------------------------------------------------------------===//

namespace {

//! Triggers every update while held (frame-coherent behavior)
NOLINT_TEST(ActionTriggerDown_PerFrame, TriggersEveryFrameWhileHeld)
{
  // Arrange
  ActionTriggerDown trigger;
  ActionValue v { false };

  // Act: press
  v.Update(true);

  // Assert: fires on each UpdateState while held
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());

  // Release ends triggering
  v.Update(false);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsCompleted());
}

//! Below actuation threshold: never triggers and stays idle
NOLINT_TEST(ActionTriggerDown_Threshold, NoTriggerBelowActuationThreshold)
{
  // Arrange
  ActionTriggerDown trigger;
  trigger.SetActuationThreshold(1.1F); // bool true (1.0) < 1.1
  ActionValue v { false };

  // Act: press -> still below threshold
  v.Update(true);
  trigger.UpdateState(v, Duration::zero());

  // Assert: never actuated
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsIdle());

  // Release keeps idle
  v.Update(false);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsIdle());
}

//! Axis inputs: triggers only when abs(value) >= threshold (positive side)
NOLINT_TEST(ActionTriggerDown_Axis, TriggersOnPositiveAboveThreshold)
{
  // Arrange
  ActionTriggerDown trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Below threshold -> no trigger
  v.Update(Axis1D { 0.39F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsIdle());

  // Cross threshold and hold -> triggers every frame
  v.Update(Axis1D { 0.41F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());

  // Release -> stops triggering
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Axis inputs: triggers only when abs(value) >= threshold (negative side)
NOLINT_TEST(ActionTriggerDown_Axis, TriggersOnNegativeAboveThreshold)
{
  // Arrange
  ActionTriggerDown trigger;
  trigger.SetActuationThreshold(0.40F);
  ActionValue v { Axis1D { 0.0F } };

  // Below threshold -> no trigger
  v.Update(Axis1D { -0.39F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
  EXPECT_TRUE(trigger.IsIdle());

  // Beyond negative threshold -> triggers per frame
  v.Update(Axis1D { -0.50F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsTriggered());

  // Back to zero -> idle
  v.Update(Axis1D { 0.0F });
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsTriggered());
}

//! Down never reports Canceled; it simply stops triggering on release
NOLINT_TEST(ActionTriggerDown_Cancellation, NeverCanceled)
{
  // Arrange
  ActionTriggerDown trigger;
  ActionValue v { false };

  // Idle -> not canceled
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsCanceled());

  // Press -> not canceled
  v.Update(true);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsCanceled());

  // Held -> still not canceled
  trigger.UpdateState(v, Duration::zero());
  EXPECT_FALSE(trigger.IsCanceled());

  // Release -> completed but not canceled
  v.Update(false);
  trigger.UpdateState(v, Duration::zero());
  EXPECT_TRUE(trigger.IsCompleted());
  EXPECT_FALSE(trigger.IsCanceled());
}

} // namespace
