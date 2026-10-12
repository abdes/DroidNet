//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Play sessions and pause as the player sees them: what a session leaves
// behind when it ends, and what a key held through a pause does on resume.

#include <chrono>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/Test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;
using std::chrono::microseconds;

namespace {

class SessionScenario : public ScenarioTest { };

//! Actions and contexts added during a session are gone after it, and the
//! contexts that existed before return to their recorded activation.
NOLINT_TEST_F(SessionScenario, SessionEndRestoresInputState)
{
  oxygen::co::Run(Loop(), [&] -> oxygen::co::Co<> {
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto base = MakeContext("base", 0);
    Map(*base, move, InputSlots::W, Explicit<ActionTriggerPressed>());
    auto open_menu = MakeAction("OpenMenu", ActionValueType::kBool);
    auto menu = MakeContext("menu", kOverlayPriority);
    Map(*menu, open_menu, InputSlots::M, Explicit<ActionTriggerPressed>());
    System().DeactivateMappingContext(menu);
    co_await Frame();

    EXPECT_TRUE(System().OnSessionBegin(nullptr));
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto session = MakeContext("session", kOverlayPriority + 1);
    Map(*session, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());
    System().ActivateMappingContext(menu);
    System().DeactivateMappingContext(base);
    SendKeyEvent(Key::kM, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(open_menu->WasTriggeredThisFrame());
    EXPECT_EQ(open_menu->GetPendingFixedStepTriggers(), 1U);
    SendKeyEvent(Key::kM, ButtonState::kReleased);
    co_await Frame();

    System().OnSessionEnd(nullptr);

    EXPECT_EQ(System().GetActionByName("Jump"), nullptr);
    EXPECT_EQ(System().GetMappingContextByName("session"), nullptr);
    EXPECT_EQ(System().GetActionByName("Move"), move);
    EXPECT_EQ(System().GetMappingContextByName("menu"), menu);
    EXPECT_EQ(open_menu->GetPendingFixedStepTriggers(), 0U);

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    SendKeyEvent(Key::kM, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(move->WasTriggeredThisFrame());
    EXPECT_FALSE(open_menu->WasTriggeredThisFrame());
  });
}

//! A key pressed before a pause and still held at resume reports held, not
//! newly triggered: input keeps evaluating while the game is paused.
NOLINT_TEST_F(SessionScenario, KeyHeldAcrossPauseIsNotRetriggered)
{
  oxygen::co::Run(Loop(), [&] -> oxygen::co::Co<> {
    auto fire = MakeAction("Fire", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, fire, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(fire->WasTriggeredThisFrame());

    constexpr auto kPaused = FrameTime {
      .game = microseconds { 0 },
      .physical = kFrame60,
    };
    for (int i = 0; i < 5; ++i) {
      co_await Frame(kPaused);
      EXPECT_FALSE(fire->WasTriggeredThisFrame());
    }

    co_await Frame();
    EXPECT_FALSE(fire->WasTriggeredThisFrame());
    EXPECT_TRUE(fire->GetValue().GetAs<bool>());
  });
}

} // namespace
