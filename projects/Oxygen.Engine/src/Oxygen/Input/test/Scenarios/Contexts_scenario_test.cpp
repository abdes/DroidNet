//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Mapping-context switches and the global input switch: an action driven by a
// context that goes away must end, visibly, and must not stick.

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::observer_ptr;
using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

namespace {

class ContextScenario : public ScenarioTest { };

//! Hold Shift to sprint, open the pause menu (gameplay context deactivated,
//! menu context activated), release Shift in the menu, close the menu. Sprint
//! ends with a Canceled edge when the menu opens and does not come back.
NOLINT_TEST_F(ContextScenario, SprintDoesNotStickAfterPauseMenu)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto sprint = MakeAction("Sprint", ActionValueType::kBool);
    auto back = MakeAction("MenuBack", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    Map(
      *gameplay, sprint, InputSlots::LeftShift, Explicit<ActionTriggerDown>());
    auto menu = MakeContext("menu", kOverlayPriority);
    Map(*menu, back, InputSlots::Escape, Explicit<ActionTriggerPressed>());
    System().DeactivateMappingContext(menu);

    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(sprint->IsOngoing());

    System().DeactivateMappingContext(gameplay);
    System().ActivateMappingContext(menu);
    co_await Frame();
    EXPECT_FALSE(sprint->IsOngoing()) << "gameplay context is inactive";
    EXPECT_TRUE(sprint->WasCanceledThisFrame()) << "the edge is observable";

    SendKeyEvent(Key::kLeftShift, ButtonState::kReleased);
    co_await Frame();
    EXPECT_FALSE(sprint->WasCanceledThisFrame()) << "one edge, not two";

    System().DeactivateMappingContext(menu);
    System().ActivateMappingContext(gameplay);
    constexpr int kOneSecond = 60;
    for (int i = 0; i < kOneSecond; ++i) {
      co_await Frame();
      EXPECT_FALSE(sprint->IsOngoing()) << "Shift is not held";
    }
  });
}

//! Deactivating and re-activating a context within the same frame keeps it
//! active: the deferred cancel is withdrawn.
NOLINT_TEST_F(ContextScenario, ReactivationInSameFrameKeepsAction)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto sprint = MakeAction("Sprint", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    Map(
      *gameplay, sprint, InputSlots::LeftShift, Explicit<ActionTriggerDown>());

    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    co_await Frame();
    System().DeactivateMappingContext(gameplay);
    System().ActivateMappingContext(gameplay);
    co_await Frame();
    EXPECT_TRUE(sprint->IsOngoing());
    EXPECT_FALSE(sprint->WasCanceledThisFrame());
  });
}

//! Level unload removes the gameplay context while the player holds W. The
//! action is canceled at once and stays idle.
NOLINT_TEST_F(ContextScenario, RemovedContextCancelsItsActions)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    Map(*gameplay, move, InputSlots::W, Explicit<ActionTriggerDown>());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(move->IsOngoing());

    System().OnFrameStart(Context());
    System().RemoveMappingContext(gameplay);
    EXPECT_FALSE(move->IsOngoing());
    EXPECT_TRUE(move->WasCanceledThisFrame());

    co_await Frame();
    EXPECT_FALSE(move->IsOngoing());
  });
}

//! The developer console turns input off (`nput.enabled 0`) while the player
//! holds W. Movement stops with a Canceled edge, keys typed into the console
//! do not reach gameplay, and movement resumes on a new press after input is
//! turned back on.
NOLINT_TEST_F(ContextScenario, DisablingInputCancelsHeldActions)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  System().RegisterConsoleBindings(observer_ptr { &console });

  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    Map(*gameplay, move, InputSlots::W, Explicit<ActionTriggerDown>());
    Map(*gameplay, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(move->IsOngoing());

    EXPECT_EQ(console.Execute("nput.enabled 0").status, ExecutionStatus::kOk);
    System().ApplyConsoleCVars(observer_ptr<const Console> { &console });
    co_await Frame();
    EXPECT_FALSE(move->IsOngoing());
    EXPECT_TRUE(move->WasCanceledThisFrame());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FALSE(jump->WasTriggeredThisFrame()) << "typed into the console";
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    SendKeyEvent(Key::kW, ButtonState::kReleased);
    co_await Frame();

    EXPECT_EQ(console.Execute("nput.enabled 1").status, ExecutionStatus::kOk);
    System().ApplyConsoleCVars(observer_ptr<const Console> { &console });
    co_await Frame();
    EXPECT_FALSE(move->IsOngoing());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(move->IsOngoing());
  });
}

} // namespace
