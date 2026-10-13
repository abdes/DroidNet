//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Time: trigger windows run on physical time (pause menus, slow motion), and
// fixed-step consumers see each trigger exactly once.

#include <chrono>
#include <memory>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::Action;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

namespace {

class TimingScenario : public ScenarioTest {
protected:
  static constexpr Duration kPaused { 0 };

  //! One frame followed by `substeps` fixed-step reads; returns how many of
  //! them saw a jump request.
  auto FrameWithFixedSteps(std::shared_ptr<Action> jump, const int substeps)
    -> oxygen::co::Co<int>
  {
    co_await Frame();
    int impulses = 0;
    for (int step = 0; step < substeps; ++step) {
      impulses += jump->ConsumeFixedStepTrigger() ? 1 : 0;
    }
    co_return impulses;
  }
};

//! Pause menu: "hold Esc for 1 s to quit to title". The game is paused
//! (game_delta_time is zero) while real frames keep running.
NOLINT_TEST_F(TimingScenario, HoldToQuitWorksWhilePaused)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto quit = MakeAction("HoldToQuit", ActionValueType::kBool);
    auto menu = MakeContext("pause menu", kOverlayPriority);
    {
      auto trigger = Explicit<ActionTriggerHold>();
      trigger->SetHoldDurationThreshold(1.0F);
      Map(*menu, quit, InputSlots::Escape, trigger);
    }

    bool fired = false;
    SendKeyEvent(Key::kEscape, ButtonState::kPressed);
    constexpr int kOneAndHalfSeconds = 90;
    for (int i = 0; i < kOneAndHalfSeconds; ++i) {
      co_await Frame({ .game = kPaused, .physical = kFrame60 });
      fired |= quit->WasTriggeredThisFrame();
    }
    EXPECT_TRUE(fired);
  });
}

//! In the pause menu a 2 s press is not a tap.
NOLINT_TEST_F(TimingScenario, LongPressWhilePausedIsNotATap)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto select = MakeAction("QuickSelect", ActionValueType::kBool);
    auto menu = MakeContext("pause menu", kOverlayPriority);
    {
      auto trigger = Explicit<ActionTriggerTap>();
      constexpr float kTapSeconds = 0.2F;
      trigger->SetTapTimeThreshold(kTapSeconds);
      Map(*menu, select, InputSlots::Return, trigger);
    }

    SendKeyEvent(Key::kReturn, ButtonState::kPressed);
    constexpr int kTwoSeconds = 120;
    for (int i = 0; i < kTwoSeconds; ++i) {
      co_await Frame({ .game = kPaused, .physical = kFrame60 });
    }
    SendKeyEvent(Key::kReturn, ButtonState::kReleased);
    co_await Frame({ .game = kPaused, .physical = kFrame60 });
    EXPECT_FALSE(select->WasTriggeredThisFrame());
  });
}

//! Bullet time at 0.25x: a 0.5 s hold-to-reload takes 0.5 s of the player's
//! time, not 2 s.
NOLINT_TEST_F(TimingScenario, HoldDurationDoesNotStretchInSlowMotion)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto reload = MakeAction("Reload", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    {
      auto trigger = Explicit<ActionTriggerHold>();
      trigger->SetHoldDurationThreshold(0.5F);
      Map(*ctx, reload, InputSlots::R, trigger);
    }

    constexpr auto kSlowFrame = kFrame60 / 4;
    SendKeyEvent(Key::kR, ButtonState::kPressed);
    co_await Frame({ .game = kSlowFrame, .physical = kFrame60 });
    int frames = 0;
    constexpr int kGiveUp = 240;
    for (; frames < kGiveUp && !reload->WasTriggeredThisFrame(); ++frames) {
      co_await Frame({ .game = kSlowFrame, .physical = kFrame60 });
    }
    const double real_seconds
      = frames * std::chrono::duration<double>(kFrame60).count();
    EXPECT_NEAR(real_seconds, 0.5, 0.034);
  });
}

//! Physics jump read from the fixed-step phase. At 144 fps with 60 Hz physics
//! some frames run no step; at 30 fps the engine runs two. One press of
//! Space gives exactly one impulse in both cases.
NOLINT_TEST_F(TimingScenario, OnePressGivesExactlyOneFixedStepJump)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    int high_fps = co_await FrameWithFixedSteps(jump, 0);
    high_fps += co_await FrameWithFixedSteps(jump, 1);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    high_fps += co_await FrameWithFixedSteps(jump, 1);
    EXPECT_EQ(high_fps, 1);

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    int low_fps = co_await FrameWithFixedSteps(jump, 2);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    low_fps += co_await FrameWithFixedSteps(jump, 2);
    EXPECT_EQ(low_fps, 1);
  });
}

//! Two presses in one rendered frame (a 30 fps frame, fast fingers) are two
//! jumps for the fixed step, one per substep.
NOLINT_TEST_F(TimingScenario, TwoPressesInOneFrameGiveTwoFixedStepJumps)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    EXPECT_EQ(co_await FrameWithFixedSteps(jump, 2), 2);
    EXPECT_EQ(jump->GetPendingFixedStepTriggers(), 0U);
  });
}

} // namespace
