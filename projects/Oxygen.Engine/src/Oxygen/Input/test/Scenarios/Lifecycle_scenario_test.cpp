//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Activation lifecycle as gameplay observes it: how an activation ends
// (Completed or Canceled), what the published snapshot holds, and how
// actions that depend on other actions (Combo, Chain) see them.

#include <chrono>
#include <memory>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::observer_ptr;
using oxygen::input::Action;
using oxygen::input::ActionState;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerCombo;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerPulse;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputSnapshot;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;
using oxygen::platform::MouseButton;
using std::chrono::milliseconds;

namespace {

//! Counts the edges an action reported over several frames.
struct EdgeLog {
  int triggered = 0;
  int completed = 0;
  int canceled = 0;

  void Sample(const Action& action)
  {
    triggered += action.WasTriggeredThisFrame() ? 1 : 0;
    completed += action.WasCompletedThisFrame() ? 1 : 0;
    canceled += action.WasCanceledThisFrame() ? 1 : 0;
  }
};

class LifecycleScenario : public ScenarioTest {
protected:
  //! Double-tap D to dash: a Combo of two triggers of `tap`.
  static auto MakeDoubleTap(const std::shared_ptr<Action>& tap)
  {
    constexpr float kWindow = 0.3F;
    auto combo = Explicit<ActionTriggerCombo>();
    combo->AddComboStep(tap, ActionState::kTriggered, kWindow);
    combo->AddComboStep(tap, ActionState::kTriggered, kWindow);
    return combo;
  }
};

//! Hold E for 0.5 s to open a door. Releasing after the door opened ends the
//! interaction with Completed, which gameplay uses to end the animation.
NOLINT_TEST_F(LifecycleScenario, HoldToInteractReportsCompleted)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto open = MakeAction("OpenDoor", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    {
      auto trigger = Explicit<ActionTriggerHold>();
      trigger->SetHoldDurationThreshold(0.5F);
      Map(*ctx, open, InputSlots::E, trigger);
    }

    constexpr milliseconds kTick { 200 };
    EdgeLog held;
    SendKeyEvent(Key::kE, ButtonState::kPressed);
    co_await Frame();
    for (int i = 0; i < 4; ++i) {
      co_await Frame(kTick);
      held.Sample(*open);
    }
    SendKeyEvent(Key::kE, ButtonState::kReleased);
    co_await Frame();
    const auto snapshot = System().GetCurrentSnapshot();
    EdgeLog released;
    released.Sample(*open);
    co_await Frame();
    released.Sample(*open);

    EXPECT_GE(held.triggered, 1);
    EXPECT_EQ(released.completed, 1);
    EXPECT_EQ(released.canceled, 0);
    EXPECT_TRUE(snapshot->DidActionComplete(open->GetAssetKey()));
  });
}

//! An activation that never triggered (released at 0.2 s of a 0.5 s hold) is
//! canceled, so the two outcomes stay distinguishable.
NOLINT_TEST_F(LifecycleScenario, AbortedHoldIsCanceled)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto open = MakeAction("OpenDoor", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    {
      auto trigger = Explicit<ActionTriggerHold>();
      trigger->SetHoldDurationThreshold(0.5F);
      Map(*ctx, open, InputSlots::E, trigger);
    }

    constexpr milliseconds kHeldFor { 200 };
    EdgeLog log;
    SendKeyEvent(Key::kE, ButtonState::kPressed);
    co_await Frame();
    co_await Frame(kHeldFor);
    log.Sample(*open);
    SendKeyEvent(Key::kE, ButtonState::kReleased);
    for (int i = 0; i < 3; ++i) {
      co_await Frame();
      log.Sample(*open);
    }
    EXPECT_EQ(log.triggered, 0);
    EXPECT_EQ(log.completed, 0);
    EXPECT_EQ(log.canceled, 1);
  });
}

//! Auto-fire: hold LMB with a Pulse every 0.1 s and release between pulses
//! after three shots. The activation succeeded: it completes, it is not
//! canceled. (Unreal documents Canceled here; see Docs/Triggers/Pulse.md.)
NOLINT_TEST_F(LifecycleScenario, AutoFireReleasedBetweenPulsesCompletes)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto fire = MakeAction("AutoFire", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    {
      auto trigger = Explicit<ActionTriggerPulse>();
      constexpr float kIntervalSeconds = 0.1F;
      trigger->SetInterval(kIntervalSeconds);
      trigger->TriggerOnStart(true);
      Map(*ctx, fire, InputSlots::LeftMouseButton, trigger);
    }

    EdgeLog log;
    SendMouseButtonEvent(MouseButton::kLeft, ButtonState::kPressed);
    co_await Frame(milliseconds { 0 });
    log.Sample(*fire);
    constexpr milliseconds kTick { 50 };
    constexpr int kTicks = 5; // 0.25 s: pulses at 0, 0.1 and 0.2 s
    for (int i = 0; i < kTicks; ++i) {
      co_await Frame(kTick);
      log.Sample(*fire);
    }
    SendMouseButtonEvent(MouseButton::kLeft, ButtonState::kReleased);
    for (int i = 0; i < 3; ++i) {
      co_await Frame();
      log.Sample(*fire);
    }
    EXPECT_EQ(log.triggered, 3);
    EXPECT_EQ(log.completed, 1);
    EXPECT_EQ(log.canceled, 0);
  });
}

//! Pressed (jump, key still held) and Tap (dodge) never hold Ongoing. Each
//! activation that triggered is closed by exactly one Completed, in the
//! following frame.
NOLINT_TEST_F(LifecycleScenario, PressedAndTapActivationsComplete)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto dodge = MakeAction("Dodge", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());
    {
      auto trigger = Explicit<ActionTriggerTap>();
      constexpr float kTapSeconds = 0.2F;
      trigger->SetTapTimeThreshold(kTapSeconds);
      Map(*ctx, dodge, InputSlots::LeftControl, trigger);
    }

    EdgeLog jump_log;
    EdgeLog dodge_log;
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kLeftControl, ButtonState::kPressed);
    co_await Frame();
    jump_log.Sample(*jump);
    dodge_log.Sample(*dodge);
    SendKeyEvent(Key::kLeftControl, ButtonState::kReleased);
    for (int i = 0; i < 4; ++i) {
      co_await Frame();
      jump_log.Sample(*jump);
      dodge_log.Sample(*dodge);
    }
    EXPECT_EQ(jump_log.triggered, 1);
    EXPECT_EQ(jump_log.completed, 1);
    EXPECT_EQ(jump_log.canceled, 0);
    EXPECT_EQ(dodge_log.triggered, 1);
    EXPECT_EQ(dodge_log.completed, 1);
    EXPECT_EQ(dodge_log.canceled, 0);
  });
}

//! The engine publishes frame N's snapshot to parallel tasks and frames in
//! flight. A reader that still holds it in frame N+1 sees frame N.
NOLINT_TEST_F(LifecycleScenario, PublishedSnapshotDoesNotChangeNextFrame)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    co_await Frame();
    const auto frame_n = System().GetCurrentSnapshot();
    EXPECT_TRUE(frame_n->DidActionTrigger(jump->GetAssetKey()));
    System().OnFrameEnd(Context());
    co_await Frame();
    EXPECT_TRUE(frame_n->DidActionTrigger(jump->GetAssetKey()));
    EXPECT_FALSE(
      System().GetCurrentSnapshot()->DidActionTrigger(jump->GetAssetKey()));
  });
}

//! A level unload removes its actions while a parallel task still holds the
//! published snapshot. Reading it stays valid (run under AddressSanitizer).
NOLINT_TEST_F(LifecycleScenario, SnapshotSurvivesActionRemoval)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("LevelJump", ActionValueType::kBool);
    auto ctx = MakeContext("level", 0);
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    co_await Frame();
    const std::shared_ptr<const InputSnapshot> held_by_task
      = System().GetCurrentSnapshot();
    const auto jump_key = jump->GetAssetKey();

    System().RemoveMappingContext(ctx);
    System().RemoveAction(jump);
    ctx.reset();
    jump.reset();

    EXPECT_TRUE(held_by_task->DidActionTrigger(jump_key));
  });
}

//! One tap of D while the mouse moves in the same frame is not a double tap:
//! the Combo reads each transition of its step action once.
NOLINT_TEST_F(LifecycleScenario, SingleTapWithMouseMoveIsNotDoubleTap)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto tap = MakeAction("TapRight", ActionValueType::kBool);
    auto dash = MakeAction("Dash", ActionValueType::kBool);
    auto look = MakeAction("Look", ActionValueType::kAxis2D);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, tap, InputSlots::D, Explicit<ActionTriggerPressed>());
    Map(*ctx, dash, InputSlots::D, MakeDoubleTap(tap));
    {
      auto trigger = Explicit<ActionTriggerDown>();
      trigger->SetActuationThreshold(0.0F);
      Map(*ctx, look, InputSlots::MouseXY, trigger);
    }

    SendKeyEvent(Key::kD, ButtonState::kPressed);
    SendMouseMotion(2.0F, 0.0F);
    co_await Frame();
    EXPECT_FALSE(dash->WasTriggeredThisFrame()) << "only one tap";
  });
}

//! A real double tap within the window dashes.
NOLINT_TEST_F(LifecycleScenario, DoubleTapDashes)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto tap = MakeAction("TapRight", ActionValueType::kBool);
    auto dash = MakeAction("Dash", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, tap, InputSlots::D, Explicit<ActionTriggerPressed>());
    Map(*ctx, dash, InputSlots::D, MakeDoubleTap(tap));

    constexpr milliseconds kTapGap { 50 };
    bool dashed = false;
    SendKeyEvent(Key::kD, ButtonState::kPressed);
    co_await Frame();
    dashed |= dash->WasTriggeredThisFrame();
    SendKeyEvent(Key::kD, ButtonState::kReleased);
    co_await Frame(kTapGap);
    dashed |= dash->WasTriggeredThisFrame();
    SendKeyEvent(Key::kD, ButtonState::kPressed);
    co_await Frame(kTapGap);
    dashed |= dash->WasTriggeredThisFrame();
    EXPECT_TRUE(dashed);
  });
}

//! Shift + W to sprint-dash, authored as a Chain that requires Shift to be
//! held: W fires while Shift is held and not after Shift was released.
NOLINT_TEST_F(LifecycleScenario, ChainRequiresPrerequisiteHeld)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto shift = MakeAction("Modifier", ActionValueType::kBool);
    auto sprint_dash = MakeAction("SprintDash", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, shift, InputSlots::LeftShift, Explicit<ActionTriggerDown>());
    {
      auto chain = Explicit<ActionTriggerChain>();
      chain->SetLinkedAction(shift);
      chain->RequirePrerequisiteHeld(true);
      Map(*ctx, sprint_dash, InputSlots::W, chain);
    }

    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(sprint_dash->WasTriggeredThisFrame()) << "Shift is held";

    SendKeyEvent(Key::kW, ButtonState::kReleased);
    SendKeyEvent(Key::kLeftShift, ButtonState::kReleased);
    co_await Frame();
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FALSE(sprint_dash->WasTriggeredThisFrame()) << "Shift was released";
  });
}

//! A Chain with a 0.3 s window: W 0.1 s after Shift fires, W 0.5 s after
//! Shift does not.
NOLINT_TEST_F(LifecycleScenario, ChainHonorsMaxDelay)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto shift = MakeAction("Modifier", ActionValueType::kBool);
    auto quick = MakeAction("QuickFollowUp", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, shift, InputSlots::LeftShift, Explicit<ActionTriggerDown>());
    {
      auto chain = Explicit<ActionTriggerChain>();
      chain->SetLinkedAction(shift);
      constexpr float kWindowSeconds = 0.3F;
      chain->SetMaxDelaySeconds(kWindowSeconds);
      Map(*ctx, quick, InputSlots::W, chain);
    }

    constexpr milliseconds kTick { 100 };
    constexpr int kTicksPastWindow = 5;
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    co_await Frame(milliseconds { 0 });
    co_await Frame(kTick);
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame(milliseconds { 0 });
    EXPECT_TRUE(quick->WasTriggeredThisFrame()) << "0.1 s after Shift";

    SendKeyEvent(Key::kW, ButtonState::kReleased);
    SendKeyEvent(Key::kLeftShift, ButtonState::kReleased);
    co_await Frame();
    co_await Frame();

    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    co_await Frame(milliseconds { 0 });
    for (int i = 0; i < kTicksPastWindow; ++i) {
      co_await Frame(kTick);
    }
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame(milliseconds { 0 });
    EXPECT_FALSE(quick->WasTriggeredThisFrame()) << "0.5 s after Shift";
  });
}

} // namespace
