//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::InputSnapshot;

namespace {

//! Basic: level flags and value reflected in snapshot at frame end
NOLINT_TEST(InputSnapshot, LevelFlagsAndValue)
{
  // Arrange
  auto a
    = std::make_shared<Action>("jump", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(Action::State { .triggered = true }, ActionValue { true });
  a->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  const auto flags = snap.GetActionStateFlags("jump");
  EXPECT_TRUE(static_cast<bool>(flags & ActionState::kTriggered));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kOngoing));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kCompleted));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kCanceled));

  EXPECT_TRUE(snap.IsActionTriggered("jump"));
  EXPECT_FALSE(snap.IsActionOngoing("jump"));
  EXPECT_FALSE(snap.IsActionCompleted("jump"));
  EXPECT_FALSE(snap.IsActionCanceled("jump"));
  EXPECT_TRUE(
    snap.IsActionIdle("nope")); // name not present -> idle defaults to true

  const auto v = snap.GetActionValue("jump");
  EXPECT_TRUE(v.GetAs<bool>());
}

//! Edge: precise transition predicate checks
NOLINT_TEST(InputSnapshot, Edge_SpecificTransitionPredicate)
{
  // Arrange
  auto a
    = std::make_shared<Action>("door", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  a->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = true,
      .canceled = false,
    },
    ActionValue { false });
  a->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionTransition(
    "door", ActionState::kTriggered, ActionState::kCompleted));
  EXPECT_FALSE(snap.DidActionTransition(
    "door", ActionState::kOngoing, ActionState::kCanceled));
}

//! Edge: immediate start via None->Triggered in the same frame
NOLINT_TEST(InputSnapshot, Edge_ImmediateStart_NoneToTriggered)
{
  // Arrange
  auto a
    = std::make_shared<Action>("fire", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(Action::State { .triggered = true }, ActionValue { true });
  a->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionStart("fire"));
  EXPECT_TRUE(snap.DidActionTrigger("fire"));
  EXPECT_FALSE(snap.DidActionRelease("fire"));
}

//! Edge: start detected when Ongoing edge occurs then Trigger in the same frame
NOLINT_TEST(InputSnapshot, Edge_Start_OngoingThenTrigger)
{
  // Arrange
  auto a
    = std::make_shared<Action>("sprint", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  a->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  a->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionStart("sprint"));
  EXPECT_TRUE(snap.DidActionTrigger("sprint"));
  EXPECT_FALSE(snap.DidActionRelease("sprint"));
}

//! Edge: release detected via Ongoing -> not Ongoing in the same frame
NOLINT_TEST(InputSnapshot, Edge_ReleaseWithinFrame)
{
  // Arrange
  auto a
    = std::make_shared<Action>("grab", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  a->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });
  a->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionRelease("grab"));
  EXPECT_FALSE(snap.DidActionTrigger("grab"));
}

//! Edge: complete and cancel detection
NOLINT_TEST(InputSnapshot, Edge_CompleteAndCancel)
{
  // Arrange
  auto ac = std::make_shared<Action>(
    "complete", oxygen::input::ActionValueType::kBool);
  ac->BeginFrameTracking();
  ac->UpdateState(
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ac->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = true,
      .canceled = false,
    },
    ActionValue { false });
  ac->EndFrameTracking();

  auto an
    = std::make_shared<Action>("cancel", oxygen::input::ActionValueType::kBool);
  an->BeginFrameTracking();
  an->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  an->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = true,
    },
    ActionValue { false });
  an->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { ac, an };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionComplete("complete"));
  EXPECT_FALSE(snap.DidActionCancel("complete"));

  EXPECT_TRUE(snap.DidActionCancel("cancel"));
  EXPECT_FALSE(snap.DidActionComplete("cancel"));
}

//! Edge: value update flag
NOLINT_TEST(InputSnapshot, Edge_ValueUpdate)
{
  // Arrange
  auto a
    = std::make_shared<Action>("move", oxygen::input::ActionValueType::kBool);
  a->BeginFrameTracking();
  a->UpdateState(
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  a->EndFrameTracking();

  auto b
    = std::make_shared<Action>("idle", oxygen::input::ActionValueType::kBool);
  b->BeginFrameTracking();
  // no updates in this frame
  b->EndFrameTracking();

  std::vector<std::shared_ptr<Action>> actions { a, b };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionValueUpdate("move"));
  EXPECT_FALSE(snap.DidActionValueUpdate("idle"));
}

//! Defaults for unknown action names
NOLINT_TEST(InputSnapshot, UnknownAction_Defaults)
{
  // Arrange
  std::vector<std::shared_ptr<Action>> actions {};

  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_EQ(snap.GetActionTransitions("nope").size(), 0u);
  EXPECT_FALSE(snap.IsActionTriggered("nope"));
  EXPECT_FALSE(snap.IsActionOngoing("nope"));
  EXPECT_FALSE(snap.IsActionCompleted("nope"));
  EXPECT_FALSE(snap.IsActionCanceled("nope"));
  EXPECT_TRUE(snap.IsActionIdle("nope"));
  EXPECT_EQ(snap.GetActionStateFlags("nope"), ActionState::kNone);
  const auto v = snap.GetActionValue("nope");
  EXPECT_FALSE(v.GetAs<bool>());
}

// (Frame start time is owned by FrameContext and not mirrored here.)

} // namespace
