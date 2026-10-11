//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Input consumption: an action that consumes input blocks lower-priority
// mappings that read the same physical source, and nothing else.

#include <chrono>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/Test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Axis1D;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHoldAndRelease;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

namespace {

class ConsumptionScenario : public ScenarioTest {
protected:
  //! A Down trigger that fires on any axis motion.
  static auto AnyMotion()
  {
    auto trigger = Explicit<ActionTriggerDown>();
    trigger->SetActuationThreshold(0.0F);
    return trigger;
  }
};

//! Walking forward (W) and pressing Interact (E) on a door. Interact consumes
//! E; movement on W is unaffected and an idle Jump gets no Canceled edge.
NOLINT_TEST_F(ConsumptionScenario, InteractDoesNotCancelMovementOnOtherKey)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto interact = MakeAction("Interact", ActionValueType::kBool);
    interact->SetConsumesInput(true);
    auto move = MakeAction("MoveForward", ActionValueType::kBool);
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, interact, InputSlots::E, Explicit<ActionTriggerPressed>());
    Map(*ctx, move, InputSlots::W, Explicit<ActionTriggerDown>());
    Map(*ctx, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kE, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(interact->WasTriggeredThisFrame());
    EXPECT_TRUE(move->IsOngoing()) << "W is held, E is a different key";
    EXPECT_FALSE(move->WasCanceledThisFrame());
    EXPECT_FALSE(jump->WasCanceledThisFrame()) << "Jump was never active";
  });
}

//! Charging a shot on J (hold 1 s, release to fire) while a HUD prompt in a
//! higher-priority context is accepted with Enter, which the HUD consumes.
//! The charge on J survives.
NOLINT_TEST_F(ConsumptionScenario, HudPromptDoesNotResetChargeOnOtherKey)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    using std::chrono::milliseconds;
    auto confirm = MakeAction("ConfirmPickup", ActionValueType::kBool);
    confirm->SetConsumesInput(true);
    auto charge = MakeAction("ChargeShot", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    {
      auto trigger = Explicit<ActionTriggerHoldAndRelease>();
      trigger->SetHoldDurationThreshold(1.0F);
      Map(*gameplay, charge, InputSlots::J, trigger);
    }
    auto hud = MakeContext("hud", kOverlayPriority);
    Map(*hud, confirm, InputSlots::Return, Explicit<ActionTriggerPressed>());

    constexpr milliseconds kTick { 100 };
    constexpr int kTicksBeforeConfirm = 6;
    constexpr int kTicksAfterConfirm = 6;
    SendKeyEvent(Key::kJ, ButtonState::kPressed);
    co_await Frame();
    for (int i = 0; i < kTicksBeforeConfirm; ++i) {
      co_await Frame(kTick);
    }
    SendKeyEvent(Key::kReturn, ButtonState::kPressed);
    co_await Frame(kTick);
    EXPECT_TRUE(confirm->WasTriggeredThisFrame());
    EXPECT_FALSE(charge->WasCanceledThisFrame());
    for (int i = 0; i < kTicksAfterConfirm; ++i) {
      co_await Frame(kTick);
    }
    SendKeyEvent(Key::kJ, ButtonState::kReleased);
    co_await Frame(milliseconds { 0 });
    EXPECT_TRUE(charge->WasTriggeredThisFrame()) << "held 1.3 s >= 1.0 s";
  });
}

//! Space (Jump, consuming, high priority) and W (Move, low priority) pressed
//! in the same frame both trigger. A low-priority Space mapping does not.
NOLINT_TEST_F(ConsumptionScenario, ConsumedKeyBlocksOnlyThatKey)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto jump = MakeAction("Jump", ActionValueType::kBool);
    jump->SetConsumesInput(true);
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto vault = MakeAction("Vault", ActionValueType::kBool);
    auto high = MakeContext("vehicle", kOverlayPriority);
    Map(*high, jump, InputSlots::Space, Explicit<ActionTriggerPressed>());
    auto low = MakeContext("on foot", 0);
    Map(*low, move, InputSlots::W, Explicit<ActionTriggerPressed>());
    Map(*low, vault, InputSlots::Space, Explicit<ActionTriggerPressed>());

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(jump->WasTriggeredThisFrame());
    EXPECT_TRUE(move->WasTriggeredThisFrame());
    EXPECT_FALSE(vault->WasTriggeredThisFrame()) << "Space was consumed";
    EXPECT_FALSE(vault->WasCanceledThisFrame()) << "Vault was never active";
  });
}

//! A consuming MouseXY binding (a UI drag) blocks a lower-priority MouseX
//! binding (camera yaw) but not W.
NOLINT_TEST_F(ConsumptionScenario, MouseXYClaimsBothAxes)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto drag = MakeAction("Drag", ActionValueType::kAxis2D);
    drag->SetConsumesInput(true);
    auto yaw = MakeAction("Yaw", ActionValueType::kAxis1D);
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto ui = MakeContext("ui", kOverlayPriority);
    Map(*ui, drag, InputSlots::MouseXY, AnyMotion());
    auto gameplay = MakeContext("gameplay", 0);
    Map(*gameplay, yaw, InputSlots::MouseX, AnyMotion());
    Map(*gameplay, move, InputSlots::W, Explicit<ActionTriggerDown>());

    constexpr float kDragDelta = 5.0F;
    SendMouseMotion(kDragDelta, 0.0F);
    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*drag).x, kDragDelta);
    EXPECT_FLOAT_EQ(yaw->GetValue().GetAs<Axis1D>().x, 0.0F);
    EXPECT_FALSE(yaw->WasTriggeredThisFrame());
    EXPECT_TRUE(move->IsOngoing());
  });
}

//! A consuming MouseWheelUp binding (weapon cycle) claims the vertical wheel
//! axis: a lower-priority MouseWheelY binding (zoom) is blocked, a
//! MouseWheelX binding (tilt-wheel strafe) is not.
NOLINT_TEST_F(ConsumptionScenario, WheelDirectionClaimsItsAxisOnly)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto cycle = MakeAction("NextWeapon", ActionValueType::kBool);
    cycle->SetConsumesInput(true);
    auto zoom = MakeAction("Zoom", ActionValueType::kAxis1D);
    auto tilt = MakeAction("Tilt", ActionValueType::kAxis1D);
    auto weapons = MakeContext("weapons", kOverlayPriority);
    Map(*weapons, cycle, InputSlots::MouseWheelUp, AnyMotion());
    auto camera = MakeContext("camera", 0);
    Map(*camera, zoom, InputSlots::MouseWheelY, AnyMotion());
    Map(*camera, tilt, InputSlots::MouseWheelX, AnyMotion());

    SendMouseWheel(1.0F, 1.0F);
    co_await Frame();
    EXPECT_TRUE(cycle->WasTriggeredThisFrame());
    EXPECT_FLOAT_EQ(zoom->GetValue().GetAs<Axis1D>().x, 0.0F);
    EXPECT_FALSE(zoom->WasTriggeredThisFrame());
    EXPECT_FLOAT_EQ(tilt->GetValue().GetAs<Axis1D>().x, 1.0F);
  });
}

//! The player walks forward (W held) when a "press any key" prompt opens
//! with a consuming AnyKey binding. Pressing Q claims Q only: the ability on Q
//! does not fire, and walking on W continues.
NOLINT_TEST_F(ConsumptionScenario, AnyKeyClaimsOnlyThePressedKey)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines) oxco-safe
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto prompt = MakeAction("PressAnyKey", ActionValueType::kBool);
    prompt->SetConsumesInput(true);
    auto ability = MakeAction("Ability", ActionValueType::kBool);
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto gameplay = MakeContext("gameplay", 0);
    Map(*gameplay, ability, InputSlots::Q, Explicit<ActionTriggerPressed>());
    Map(*gameplay, move, InputSlots::W, Explicit<ActionTriggerDown>());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(move->IsOngoing());

    auto overlay = MakeContext("overlay", kOverlayPriority);
    Map(*overlay, prompt, InputSlots::AnyKey, Explicit<ActionTriggerPressed>());
    SendKeyEvent(Key::kQ, ButtonState::kPressed);
    co_await Frame();
    EXPECT_TRUE(prompt->WasTriggeredThisFrame());
    EXPECT_FALSE(ability->WasTriggeredThisFrame()) << "Q was consumed";
    EXPECT_TRUE(move->IsOngoing()) << "W was not claimed";
    EXPECT_FALSE(move->WasCanceledThisFrame());
  });
}

} // namespace
