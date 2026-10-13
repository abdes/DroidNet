//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/ActionTestAccess.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::testing::ActionTestAccess;

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
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = true, .ongoing = false },
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

  ActionTestAccess::BeginFrame(a);

  // Idle -> Triggered
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = true, .ongoing = false },
    ActionValue { true });

  // Triggered -> Ongoing
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });

  // Ongoing -> Completed
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = false, .ongoing = false, .completed = true },
    ActionValue { false });

  auto transitions = a.GetFrameTransitions();
  EXPECT_GE(static_cast<int>(transitions.size()), 3);
  EXPECT_FALSE(transitions.back().value_at_transition.GetAs<bool>());

  ActionTestAccess::EndFrame(a);
}

//! BeginFrameTracking clears previous frame transitions and snapshots state
NOLINT_TEST(Action, BeginFrameClearsTransitions)
{
  Action a { "start_state_test", oxygen::input::ActionValueType::kBool };

  // Pre-frame: set some state
  ActionTestAccess::UpdateState(
    a, Action::State { .triggered = true }, ActionValue { true });

  ActionTestAccess::BeginFrame(a);
  // Change to completed
  ActionTestAccess::UpdateState(
    a, Action::State { .completed = true }, ActionValue { false });

  auto transitions = a.GetFrameTransitions();
  const bool saw_completed = std::ranges::any_of(
    transitions, [](const Action::FrameTransition& t) -> bool {
      return (t.to_state & ActionState::kCompleted) == ActionState::kCompleted;
    });
  EXPECT_TRUE(saw_completed);

  ActionTestAccess::EndFrame(a);

  // Next frame: transitions cleared
  ActionTestAccess::BeginFrame(a);
  EXPECT_EQ(static_cast<int>(a.GetFrameTransitions().size()), 0);
  ActionTestAccess::EndFrame(a);
}

NOLINT_TEST(Action, NoTransitionOnSameState)
{
  Action a { "no_change", oxygen::input::ActionValueType::kBool };
  ActionTestAccess::BeginFrame(a);
  Action::State idle {};
  ActionTestAccess::UpdateState(a, idle, ActionValue { false });
  EXPECT_EQ(static_cast<int>(a.GetFrameTransitions().size()), 0);
  ActionTestAccess::EndFrame(a);
}

NOLINT_TEST(Action, DuplicateStateIgnored)
{
  Action a { "dup", oxygen::input::ActionValueType::kBool };
  ActionTestAccess::BeginFrame(a);
  Action::State trig { .triggered = true };
  ActionTestAccess::UpdateState(a, trig, ActionValue { true });
  ActionTestAccess::UpdateState(a, trig, ActionValue { true }); // duplicate
  Action::State comp { .completed = true };
  ActionTestAccess::UpdateState(a, comp, ActionValue { false });
  EXPECT_GE(static_cast<int>(a.GetFrameTransitions().size()), 2);
  ActionTestAccess::EndFrame(a);
}

//! Verify convenience edge queries against transitions
NOLINT_TEST(Action, ConvenienceEdgeQueries)
{
  Action a { "edges", oxygen::input::ActionValueType::kBool };
  // Simulate press and hold within a frame
  ActionTestAccess::BeginFrame(a);
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });
  EXPECT_TRUE(a.WasTriggeredThisFrame());
  EXPECT_TRUE(a.WasStartedThisFrame()); // Idle -> Ongoing happened
  EXPECT_FALSE(a.WasCompletedThisFrame());
  EXPECT_FALSE(a.WasCanceledThisFrame());
  EXPECT_FALSE(a.WasReleasedThisFrame());
  ActionTestAccess::EndFrame(a);

  // Next frame: release to idle; verify Released edge
  ActionTestAccess::BeginFrame(a);
  ActionTestAccess::UpdateState(
    a, Action::State { .ongoing = false }, ActionValue { false });
  EXPECT_TRUE(a.WasReleasedThisFrame());
  ActionTestAccess::EndFrame(a);
}

} // namespace

namespace {

//! Edge clearing at frame start and value update tracking
NOLINT_TEST(Action, EdgeClearingAndValueUpdates)
{
  Action a { "value_updates", oxygen::input::ActionValueType::kBool };

  // Frame 1: press and update value
  ActionTestAccess::BeginFrame(a);
  ActionTestAccess::UpdateState(a,
    Action::State { .triggered = true, .ongoing = true }, ActionValue { true });
  EXPECT_TRUE(a.WasValueUpdatedThisFrame());
  EXPECT_TRUE(a.WasTriggeredThisFrame());
  ActionTestAccess::EndFrame(a);

  // Frame 2: no updates; edges clear; ongoing persists
  ActionTestAccess::BeginFrame(a);
  EXPECT_FALSE(a.WasValueUpdatedThisFrame());
  EXPECT_FALSE(a.WasTriggeredThisFrame());
  EXPECT_TRUE(a.IsOngoing());
  ActionTestAccess::EndFrame(a);
}

} // namespace
