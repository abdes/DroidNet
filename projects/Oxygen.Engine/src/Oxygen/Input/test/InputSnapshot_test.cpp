//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/test/ActionTestAccess.h>
#include <Oxygen/Input/test/InputTestOwner.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::MakeCodeAction;
using oxygen::input::testing::kTestInputOwner;

using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionValue;
using oxygen::input::InputSnapshot;
using oxygen::input::testing::ActionTestAccess;

namespace {

//! Basic: level flags and value reflected in snapshot at frame end
NOLINT_TEST(InputSnapshot, LevelFlagsAndValue)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "jump", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(
    *a, Action::State { .triggered = true }, ActionValue { true });
  ActionTestAccess::EndFrame(*a);

  const auto nope = MakeCodeAction(
    kTestInputOwner, "nope", oxygen::input::ActionValueType::kBool);
  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  const auto flags = snap.GetActionStateFlags(a->GetAssetKey());
  EXPECT_TRUE(static_cast<bool>(flags & ActionState::kTriggered));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kOngoing));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kCompleted));
  EXPECT_FALSE(static_cast<bool>(flags & ActionState::kCanceled));

  EXPECT_TRUE(snap.IsActionTriggered(a->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionOngoing(a->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionCompleted(a->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionCanceled(a->GetAssetKey()));
  EXPECT_TRUE(snap.IsActionIdle(
    nope->GetAssetKey())); // key not present -> idle defaults to true

  const auto v = snap.GetActionValue(a->GetAssetKey());
  EXPECT_TRUE(v.GetAs<bool>());
}

//! Edge: precise transition predicate checks
NOLINT_TEST(InputSnapshot, Edge_SpecificTransitionPredicate)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "door", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = true,
      .canceled = false,
    },
    ActionValue { false });
  ActionTestAccess::EndFrame(*a);

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionTransition(
    a->GetAssetKey(), ActionState::kTriggered, ActionState::kCompleted));
  EXPECT_FALSE(snap.DidActionTransition(
    a->GetAssetKey(), ActionState::kOngoing, ActionState::kCanceled));
}

//! Edge: immediate start via None->Triggered in the same frame
NOLINT_TEST(InputSnapshot, Edge_ImmediateStart_NoneToTriggered)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "fire", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(
    *a, Action::State { .triggered = true }, ActionValue { true });
  ActionTestAccess::EndFrame(*a);

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionStart(a->GetAssetKey()));
  EXPECT_TRUE(snap.DidActionTrigger(a->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionRelease(a->GetAssetKey()));
}

//! Edge: start detected when Ongoing edge occurs then Trigger in the same frame
NOLINT_TEST(InputSnapshot, Edge_Start_OngoingThenTrigger)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "sprint", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = true,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::EndFrame(*a);

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionStart(a->GetAssetKey()));
  EXPECT_TRUE(snap.DidActionTrigger(a->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionRelease(a->GetAssetKey()));
}

//! Edge: release detected via Ongoing -> not Ongoing in the same frame
NOLINT_TEST(InputSnapshot, Edge_ReleaseWithinFrame)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "grab", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { false });
  ActionTestAccess::EndFrame(*a);

  std::vector<std::shared_ptr<Action>> actions { a };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionRelease(a->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionTrigger(a->GetAssetKey()));
}

//! Edge: complete and cancel detection
NOLINT_TEST(InputSnapshot, Edge_CompleteAndCancel)
{
  // Arrange
  auto ac = MakeCodeAction(
    kTestInputOwner, "complete", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*ac);
  ActionTestAccess::UpdateState(*ac,
    Action::State {
      .triggered = true,
      .ongoing = false,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::UpdateState(*ac,
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = true,
      .canceled = false,
    },
    ActionValue { false });
  ActionTestAccess::EndFrame(*ac);

  auto an = MakeCodeAction(
    kTestInputOwner, "cancel", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*an);
  ActionTestAccess::UpdateState(*an,
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::UpdateState(*an,
    Action::State {
      .triggered = false,
      .ongoing = false,
      .completed = false,
      .canceled = true,
    },
    ActionValue { false });
  ActionTestAccess::EndFrame(*an);

  std::vector<std::shared_ptr<Action>> actions { ac, an };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionComplete(ac->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionCancel(ac->GetAssetKey()));

  EXPECT_TRUE(snap.DidActionCancel(an->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionComplete(an->GetAssetKey()));
}

//! Edge: value update flag
NOLINT_TEST(InputSnapshot, Edge_ValueUpdate)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "move", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(*a,
    Action::State {
      .triggered = false,
      .ongoing = true,
      .completed = false,
      .canceled = false,
    },
    ActionValue { true });
  ActionTestAccess::EndFrame(*a);

  auto b = MakeCodeAction(
    kTestInputOwner, "idle", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*b);
  // no updates in this frame
  ActionTestAccess::EndFrame(*b);

  std::vector<std::shared_ptr<Action>> actions { a, b };
  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_TRUE(snap.DidActionValueUpdate(a->GetAssetKey()));
  EXPECT_FALSE(snap.DidActionValueUpdate(b->GetAssetKey()));
}

//! Defaults for an action the snapshot does not hold
NOLINT_TEST(InputSnapshot, UnknownAction_Defaults)
{
  // Arrange
  const auto nope = MakeCodeAction(
    kTestInputOwner, "nope", oxygen::input::ActionValueType::kBool);
  std::vector<std::shared_ptr<Action>> actions {};

  // Act
  InputSnapshot snap { actions };

  // Assert
  EXPECT_EQ(snap.GetActionTransitions(nope->GetAssetKey()).size(), 0U);
  EXPECT_FALSE(snap.IsActionTriggered(nope->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionOngoing(nope->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionCompleted(nope->GetAssetKey()));
  EXPECT_FALSE(snap.IsActionCanceled(nope->GetAssetKey()));
  EXPECT_TRUE(snap.IsActionIdle(nope->GetAssetKey()));
  EXPECT_EQ(snap.GetActionStateFlags(nope->GetAssetKey()), ActionState::kNone);
  const auto v = snap.GetActionValue(nope->GetAssetKey());
  EXPECT_FALSE(v.GetAs<bool>());
}

// (Frame start time is owned by FrameContext and not mirrored here.)

//! The snapshot is published into FrameContext for later phases and frames in
//! flight; starting the next frame must not change what it reports.
NOLINT_TEST(InputSnapshot, FrozenAcrossNextFrameStart)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "jump", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(
    *a, Action::State { .triggered = true }, ActionValue { true });
  ActionTestAccess::EndFrame(*a);
  std::vector<std::shared_ptr<Action>> actions { a };
  const InputSnapshot snap { actions };

  // Act: the next frame starts and clears the action's edges
  ActionTestAccess::BeginFrame(*a);

  // Assert
  EXPECT_TRUE(snap.IsActionTriggered(a->GetAssetKey()));
  EXPECT_TRUE(snap.DidActionTrigger(a->GetAssetKey()));
  EXPECT_EQ(snap.GetActionTransitions(a->GetAssetKey()).size(), 1U);
}

//! A snapshot must stay readable after its actions are destroyed (level
//! unload while a parallel or render task still holds the snapshot).
NOLINT_TEST(InputSnapshot, OutlivesItsActions)
{
  // Arrange
  auto a = MakeCodeAction(
    kTestInputOwner, "jump", oxygen::input::ActionValueType::kBool);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::UpdateState(
    *a, Action::State { .triggered = true }, ActionValue { true });
  std::vector<std::shared_ptr<Action>> actions { a };
  const InputSnapshot snap { actions };
  const auto key = a->GetAssetKey();

  // Act: drop every owner of the action
  actions.clear();
  a.reset();

  // Assert
  EXPECT_TRUE(snap.DidActionTrigger(key));
  EXPECT_TRUE(snap.GetActionValue(key).GetAs<bool>());
}

//! Actions hydrated from different assets may share a name; a query by
//! asset key reads only its own action.
NOLINT_TEST(InputSnapshot, ActionsSharingANameStayDistinctByKey)
{
  // Arrange
  using oxygen::data::AssetKey;
  const auto key_a = AssetKey::FromVirtualPath("/Content/A/Jump.oiact");
  const auto key_b = AssetKey::FromVirtualPath("/Content/B/Jump.oiact");
  auto a = std::make_shared<Action>(
    "Jump", oxygen::input::ActionValueType::kBool, key_a);
  auto b = std::make_shared<Action>(
    "Jump", oxygen::input::ActionValueType::kBool, key_b);
  ActionTestAccess::BeginFrame(*a);
  ActionTestAccess::BeginFrame(*b);
  ActionTestAccess::UpdateState(
    *a, Action::State { .triggered = true }, ActionValue { true });

  // Act
  const InputSnapshot snap { std::vector { a, b } };

  // Assert
  EXPECT_TRUE(snap.DidActionTrigger(key_a));
  EXPECT_FALSE(snap.DidActionTrigger(key_b));
  EXPECT_FALSE(snap.GetActionValue(key_b).GetAs<bool>());
  EXPECT_FALSE(snap.DidActionTrigger(AssetKey {}));
  EXPECT_EQ(snap.GetActionKeys(), (std::vector { key_a, key_b }));
}

} // namespace
