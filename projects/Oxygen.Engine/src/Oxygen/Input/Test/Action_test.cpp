//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <ranges>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>

using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;

namespace {

//! State <-> flags conversions (snapshot only; no synthetic Started)
NOLINT_TEST(ActionStateConversion, SnapshotFlags)
{
  // Idle snapshot should have no flags set (no synthetic Started)
  auto s = Action::State {
    .triggered = false,
    .ongoing = false,
    .completed = false,
    .canceled = false,
  };
  EXPECT_EQ(s.ToActionState(), ActionState::kNone);

  // Triggered
  s = Action::State {
    .triggered = true,
    .ongoing = false,
    .completed = false,
    .canceled = false,
  };
  EXPECT_EQ(
    s.ToActionState() & ActionState::kTriggered, ActionState::kTriggered);

  // Ongoing
  s = Action::State {
    .triggered = false,
    .ongoing = true,
    .completed = false,
    .canceled = false,
  };
  EXPECT_EQ(s.ToActionState() & ActionState::kOngoing, ActionState::kOngoing);

  // Completed
  s = Action::State {
    .triggered = false,
    .ongoing = false,
    .completed = true,
    .canceled = false,
  };
  EXPECT_EQ(
    s.ToActionState() & ActionState::kCompleted, ActionState::kCompleted);

  // Canceled
  s = Action::State {
    .triggered = false,
    .ongoing = false,
    .completed = false,
    .canceled = true,
  };
  EXPECT_EQ(s.ToActionState() & ActionState::kCanceled, ActionState::kCanceled);
}

NOLINT_TEST(ActionStateConversion, RoundTrip)
{
  auto bits = ActionState::kTriggered | ActionState::kOngoing;
  auto state = Action::State::FromActionState(bits);
  EXPECT_TRUE(state.triggered);
  EXPECT_TRUE(state.ongoing);
  EXPECT_FALSE(state.completed);
  EXPECT_FALSE(state.canceled);
  auto back = state.ToActionState();
  EXPECT_EQ(back & ActionState::kTriggered, ActionState::kTriggered);
  EXPECT_EQ(back & ActionState::kOngoing, ActionState::kOngoing);
}

//== Action class tests ===---------------------------------------------------//

NOLINT_TEST(Action, BasicGettersAndValue)
{
  // Arrange
  Action a { "test", oxygen::input::ActionValueType::kBool };

  // Initially idle
  EXPECT_TRUE(a.IsIdle());
  EXPECT_EQ(a.GetValueType(), oxygen::input::ActionValueType::kBool);

  // Act
  a.UpdateState(Action::State { .triggered = true, .ongoing = false },
    ActionValue { true });

  // Assert
  EXPECT_TRUE(a.IsTriggered());
  EXPECT_FALSE(a.IsOngoing());
  EXPECT_TRUE(a.GetValue().GetAs<bool>());
}

//! Transitions are only recorded within a frame window
NOLINT_TEST(Action, FrameTransitionsWithinFrame)
{
  Action a { "frame", oxygen::input::ActionValueType::kBool };

  a.BeginFrameTracking();

  // Idle -> Triggered
  a.UpdateState(Action::State { .triggered = true, .ongoing = false },
    ActionValue { true });

  // Triggered -> Ongoing
  a.UpdateState(
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });

  // Ongoing -> Completed
  a.UpdateState(
    Action::State { .triggered = false, .ongoing = false, .completed = true },
    ActionValue { false });

  auto transitions = a.GetFrameTransitions();
  EXPECT_GE(static_cast<int>(transitions.size()), 3);
  EXPECT_FALSE(transitions.back().value_at_transition.GetAs<bool>());

  a.EndFrameTracking();
}

//! BeginFrameTracking clears previous frame transitions and snapshots state
NOLINT_TEST(Action, BeginFrameClearsTransitions)
{
  Action a { "start_state_test", oxygen::input::ActionValueType::kBool };

  // Pre-frame: set some state
  a.UpdateState(Action::State { .triggered = true }, ActionValue { true });

  a.BeginFrameTracking();
  // Change to completed
  a.UpdateState(Action::State { .completed = true }, ActionValue { false });

  auto transitions = a.GetFrameTransitions();
  const bool saw_completed = std::ranges::any_of(
    transitions, [](const Action::FrameTransition& t) {
      return (t.to_state & ActionState::kCompleted) == ActionState::kCompleted;
    });
  EXPECT_TRUE(saw_completed);

  a.EndFrameTracking();

  // Next frame: transitions cleared
  a.BeginFrameTracking();
  EXPECT_EQ(static_cast<int>(a.GetFrameTransitions().size()), 0);
  a.EndFrameTracking();
}

NOLINT_TEST(Action, NoTransitionOnSameState)
{
  Action a { "no_change", oxygen::input::ActionValueType::kBool };
  a.BeginFrameTracking();
  Action::State idle {};
  a.UpdateState(idle, ActionValue { false });
  EXPECT_EQ(static_cast<int>(a.GetFrameTransitions().size()), 0);
  a.EndFrameTracking();
}

NOLINT_TEST(Action, DuplicateStateIgnored)
{
  Action a { "dup", oxygen::input::ActionValueType::kBool };
  a.BeginFrameTracking();
  Action::State trig { .triggered = true };
  a.UpdateState(trig, ActionValue { true });
  a.UpdateState(trig, ActionValue { true }); // duplicate
  Action::State comp { .completed = true };
  a.UpdateState(comp, ActionValue { false });
  EXPECT_GE(static_cast<int>(a.GetFrameTransitions().size()), 2);
  a.EndFrameTracking();
}

//! Verify convenience edge queries against transitions
NOLINT_TEST(Action, ConvenienceEdgeQueries)
{
  Action a { "edges", oxygen::input::ActionValueType::kBool };
  // Simulate press and hold within a frame
  a.BeginFrameTracking();
  a.UpdateState(
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });
  EXPECT_TRUE(a.WasTriggeredThisFrame());
  EXPECT_TRUE(a.WasStartedThisFrame()); // Idle -> Ongoing happened
  EXPECT_FALSE(a.WasCompletedThisFrame());
  EXPECT_FALSE(a.WasCanceledThisFrame());
  EXPECT_FALSE(a.WasReleasedThisFrame());
  a.EndFrameTracking();

  // Next frame: release to idle; verify Released edge
  a.BeginFrameTracking();
  a.UpdateState(Action::State { .ongoing = false }, ActionValue { false });
  EXPECT_TRUE(a.WasReleasedThisFrame());
  a.EndFrameTracking();
}

} // namespace

namespace {

//! Edge clearing at frame start and value update tracking
NOLINT_TEST(Action, EdgeClearingAndValueUpdates)
{
  Action a { "value_updates", oxygen::input::ActionValueType::kBool };

  // Frame 1: press and update value
  a.BeginFrameTracking();
  a.UpdateState(
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });
  EXPECT_TRUE(a.WasValueUpdatedThisFrame());
  EXPECT_TRUE(a.WasTriggeredThisFrame());
  a.EndFrameTracking();

  // Frame 2: no updates; edges clear; ongoing persists
  a.BeginFrameTracking();
  EXPECT_FALSE(a.WasValueUpdatedThisFrame());
  EXPECT_FALSE(a.WasTriggeredThisFrame());
  EXPECT_TRUE(a.IsOngoing());
  a.EndFrameTracking();
}

} // namespace
