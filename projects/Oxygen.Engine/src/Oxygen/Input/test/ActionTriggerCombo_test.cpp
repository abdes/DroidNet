//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Tests for ActionTriggerCombo

#include <chrono>
#include <memory>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/ActionTestAccess.h>
#include <Oxygen/Input/test/InputTestOwner.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::MakeCodeAction;
using oxygen::input::testing::kTestInputOwner;

using oxygen::input::Action;
using oxygen::input::ActionTriggerCombo;
using oxygen::input::ActionValue;
using oxygen::input::testing::ActionTestAccess;
using oxygen::time::CanonicalDuration;
using namespace std::chrono_literals;

namespace {

NOLINT_TEST(ActionTriggerCombo, TriggersWhenStepsCompleteInOrder)
{
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);

  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);

  // Step 1: A triggers
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  // Update combo
  ActionValue dummy { false };
  combo.UpdateState(dummy, CanonicalDuration { 100ms });
  EXPECT_FALSE(combo.IsTriggered());

  // Step 2: B triggers within time
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });

  combo.UpdateState(dummy, CanonicalDuration { 100ms });
  EXPECT_TRUE(combo.IsTriggered());
}

NOLINT_TEST(ActionTriggerCombo, BreakerResetsProgress)
{
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  auto X = MakeCodeAction(
    kTestInputOwner, "X", oxygen::input::ActionValueType::kBool); // breaker

  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboBreaker(X, oxygen::input::ActionState::kTriggered);

  ActionValue dummy { false };

  // Start sequence with A
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  constexpr float kElapsedSeconds = 0.1F;
  combo.UpdateState(dummy,
    CanonicalDuration { std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(kElapsedSeconds)) });

  // Breaker triggers -> should reset
  ActionTestAccess::BeginFrame(*X);
  ActionTestAccess::UpdateState(*X,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});

  // B triggers (out-of-order) -> should not complete
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(combo.IsTriggered());
}

NOLINT_TEST(ActionTriggerCombo, OutOfOrderStepResets)
{
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.5F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.5F);

  ActionValue dummy { false };

  // Fire B first -> combo should reset/ignore and not trigger
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(combo.IsTriggered());
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional Scenarios for Combo Trigger
//===----------------------------------------------------------------------===//

namespace {

//! Step timeout resets combo if exceeded (timeout applies from step 2 onwards)
NOLINT_TEST(ActionTriggerCombo, StepTimeoutResets)
{
  // Arrange
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  constexpr float kStepWindowSeconds = 0.20F;
  combo.AddComboStep(
    A, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);
  combo.AddComboStep(
    B, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);

  ActionValue dummy { false };

  // Step 1: Trigger A
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});

  // Wait beyond B's allowed window
  // Clear per-action frame states to avoid residual Triggered flags, and
  // accumulate timeout across two updates to ensure deterministic reset
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::BeginFrame(*B);
  combo.UpdateState(dummy, CanonicalDuration { 300ms });
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::BeginFrame(*B);
  combo.UpdateState(dummy, CanonicalDuration { 300ms });

  // Now trigger B -> should not complete (combo reset to step 0)
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(combo.IsTriggered());
}

//! Boundary: completing within exactly the allowed delay should succeed
NOLINT_TEST(ActionTriggerCombo, BoundaryExactDelaySucceeds)
{
  // Arrange
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  constexpr float kStepWindowSeconds = 0.10F;
  combo.AddComboStep(
    A, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);
  combo.AddComboStep(
    B, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);

  ActionValue dummy { false };

  // Step 1
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});

  // Wait exactly boundary
  combo.UpdateState(dummy, CanonicalDuration { 100ms });

  // Step 2 at boundary -> should complete
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_TRUE(combo.IsTriggered());
}

//! First step has no timeout; long wait before B is acceptable as long as
//! B's own window is measured after step 1 is done (here we trigger B
//! immediately).
NOLINT_TEST(ActionTriggerCombo, FirstStepHasNoTimeout)
{
  // Arrange
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  constexpr float kStepWindowSeconds = 0.05F;
  combo.AddComboStep(
    A, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);
  combo.AddComboStep(
    B, oxygen::input::ActionState::kTriggered, kStepWindowSeconds);

  ActionValue dummy { false };

  // Wait a long time before even starting the combo
  combo.UpdateState(dummy, CanonicalDuration { 10s });
  EXPECT_FALSE(combo.IsTriggered());

  // Start with A
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});

  // Trigger B immediately within its window
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_TRUE(combo.IsTriggered());
}

//! After a successful combo, the next sequence should start fresh at step 0
NOLINT_TEST(ActionTriggerCombo, ResetsAfterCompletion)
{
  // Arrange
  auto A = MakeCodeAction(
    kTestInputOwner, "A", oxygen::input::ActionValueType::kBool);
  auto B = MakeCodeAction(
    kTestInputOwner, "B", oxygen::input::ActionValueType::kBool);
  ActionTriggerCombo combo;
  combo.AddComboStep(A, oxygen::input::ActionState::kTriggered, 0.50F);
  combo.AddComboStep(B, oxygen::input::ActionState::kTriggered, 0.50F);

  ActionValue dummy { false };

  // Complete once: A then B
  ActionTestAccess::BeginFrame(*A);
  ActionTestAccess::UpdateState(*A,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_TRUE(combo.IsTriggered());

  // Start again: must require A first, B alone shouldn't complete
  ActionTestAccess::BeginFrame(*B);
  ActionTestAccess::UpdateState(*B,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    dummy);
  combo.UpdateState(dummy, CanonicalDuration {});
  EXPECT_FALSE(combo.IsTriggered());
}

} // namespace
