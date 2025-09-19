//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerCombo

#include <memory>

#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Duration;
using oxygen::SecondsToDuration;
using oxygen::input::Action;
using oxygen::input::ActionTriggerCombo;
using oxygen::input::ActionValue;

NOLINT_TEST(ActionTriggerCombo_Basic, TriggersWhenStepsCompleteInOrder)
{
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);

  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);

  // Step 1: A triggers
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  // Update combo
  ActionValue dummy { false };
  combo.UpdateState(dummy, SecondsToDuration(0.1F));
  EXPECT_FALSE(combo.IsTriggered());

  // Step 2: B triggers within time
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  combo.UpdateState(dummy, SecondsToDuration(0.1F));
  EXPECT_TRUE(combo.IsTriggered());
}

NOLINT_TEST(ActionTriggerCombo_Edge, BreakerResetsProgress)
{
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  auto X = std::make_shared<Action>(
    "X", oxygen::input::ActionValueType::kBool); // breaker

  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboBreaker(X, oxygen::input::ActionState::kTriggered);

  ActionValue dummy { false };

  // Start sequence with A
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.1F));

  // Breaker triggers -> should reset
  X->BeginFrameTracking();
  X->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));

  // B triggers (out-of-order) -> should not complete
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_FALSE(combo.IsTriggered());
}

NOLINT_TEST(ActionTriggerCombo_Edge, OutOfOrderStepResets)
{
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);

  ActionValue dummy { false };

  // Fire B first -> combo should reset/ignore and not trigger
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_FALSE(combo.IsTriggered());
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional Scenarios for Combo Trigger
//===----------------------------------------------------------------------===//

namespace {

//! Step timeout resets combo if exceeded (timeout applies from step 2 onwards)
NOLINT_TEST(ActionTriggerCombo_Timing, StepTimeoutResets)
{
  // Arrange
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.20F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.20F);

  ActionValue dummy { false };

  // Step 1: Trigger A
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));

  // Wait beyond B's allowed window
  // Clear per-action frame states to avoid residual Triggered flags, and
  // accumulate timeout across two updates to ensure deterministic reset
  A->BeginFrameTracking();
  B->BeginFrameTracking();
  combo.UpdateState(dummy, SecondsToDuration(0.30F));
  A->BeginFrameTracking();
  B->BeginFrameTracking();
  combo.UpdateState(dummy, SecondsToDuration(0.30F));

  // Now trigger B -> should not complete (combo reset to step 0)
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_FALSE(combo.IsTriggered());
}

//! Boundary: completing within exactly the allowed delay should succeed
NOLINT_TEST(ActionTriggerCombo_Timing, BoundaryExactDelaySucceeds)
{
  // Arrange
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.10F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.10F);

  ActionValue dummy { false };

  // Step 1
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));

  // Wait exactly boundary
  combo.UpdateState(dummy, SecondsToDuration(0.10F));

  // Step 2 at boundary -> should complete
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_TRUE(combo.IsTriggered());
}

//! First step has no timeout; long wait before B is acceptable as long as
//! B's own window is measured after step 1 is done (here we trigger B
//! immediately).
NOLINT_TEST(ActionTriggerCombo_Timing, FirstStepHasNoTimeout)
{
  // Arrange
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.05F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.05F);

  ActionValue dummy { false };

  // Wait a long time before even starting the combo
  combo.UpdateState(dummy, SecondsToDuration(10.0F));
  EXPECT_FALSE(combo.IsTriggered());

  // Start with A
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));

  // Trigger B immediately within its window
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_TRUE(combo.IsTriggered());
}

//! After a successful combo, the next sequence should start fresh at step 0
NOLINT_TEST(ActionTriggerCombo_Repeat, ResetsAfterCompletion)
{
  // Arrange
  auto A = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto B = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.50F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.50F);

  ActionValue dummy { false };

  // Complete once: A then B
  A->BeginFrameTracking();
  A->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_TRUE(combo.IsTriggered());

  // Start again: must require A first, B alone shouldn't complete
  B->BeginFrameTracking();
  B->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, SecondsToDuration(0.0F));
  EXPECT_FALSE(combo.IsTriggered());
}

} // namespace
