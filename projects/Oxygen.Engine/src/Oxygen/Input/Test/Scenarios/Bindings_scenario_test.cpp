//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Several bindings driving one action: state combination, scale and bias,
// and accumulation modes.

#include <memory>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/Test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::ActionAccumulation;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionValueType;
using oxygen::input::InputMappingContext;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;
using oxygen::platform::MouseButton;

namespace {

class BindingScenario : public ScenarioTest {
protected:
  //! WASD on one 2D action: D = +X, A = -X, W = +Y, S = -Y (by scale).
  auto MakeWasdMove(InputMappingContext& context, ActionAccumulation mode)
  {
    auto move = MakeAction("Move", ActionValueType::kAxis2D);
    move->SetAccumulation(mode);
    BindAxis(context, move, InputSlots::D, { .x = 1.0F, .y = 0.0F });
    BindAxis(context, move, InputSlots::A, { .x = -1.0F, .y = 0.0F });
    BindAxis(context, move, InputSlots::W, { .x = 0.0F, .y = 1.0F });
    BindAxis(context, move, InputSlots::S, { .x = 0.0F, .y = -1.0F });
    return move;
  }

  static void BindAxis(InputMappingContext& context,
    const std::shared_ptr<oxygen::input::Action>& action, const InputSlot& slot,
    const Axis2D& scale)
  {
    Map(context, action, slot, Explicit<ActionTriggerDown>())->SetScale(scale);
  }
};

//! Third-person movement: W, A, S and D all drive a boolean Move. Releasing
//! the strafe key while W is held must not stop the character.
NOLINT_TEST_F(BindingScenario, ReleasingOneOfSeveralKeysKeepsActionOngoing)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto move = MakeAction("Move", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, move, InputSlots::W, Explicit<ActionTriggerDown>());
    Map(*ctx, move, InputSlots::A, Explicit<ActionTriggerDown>());
    Map(*ctx, move, InputSlots::S, Explicit<ActionTriggerDown>());
    Map(*ctx, move, InputSlots::D, Explicit<ActionTriggerDown>());

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kA, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kA, ButtonState::kReleased);
    co_await Frame();
    EXPECT_TRUE(move->IsOngoing()) << "W is still held";
    EXPECT_FALSE(move->WasReleasedThisFrame());

    co_await Frame();
    EXPECT_TRUE(move->IsOngoing());
    EXPECT_FALSE(move->WasStartedThisFrame()) << "Move never stopped";
  });
}

//! Shooter: Fire on both LMB and Ctrl. Releasing Ctrl while LMB is held must
//! not stop automatic fire.
NOLINT_TEST_F(BindingScenario, AlternateBindingReleaseKeepsAutoFire)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto fire = MakeAction("Fire", ActionValueType::kBool);
    auto ctx = MakeContext("gameplay", 0);
    Map(*ctx, fire, InputSlots::LeftMouseButton, Explicit<ActionTriggerDown>());
    Map(*ctx, fire, InputSlots::LeftControl, Explicit<ActionTriggerDown>());

    SendMouseButtonEvent(MouseButton::kLeft, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kLeftControl, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kLeftControl, ButtonState::kReleased);
    co_await Frame();
    EXPECT_TRUE(fire->IsOngoing()) << "LMB is still held";
  });
}

//! Scale maps keys onto 2D axes: W with scale (0, 1) gives (0, 1), S with
//! scale (0, -1) gives (0, -1).
NOLINT_TEST_F(BindingScenario, ScaleMapsKeysOntoAxes)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("gameplay", 0);
    auto move = MakeWasdMove(*ctx, ActionAccumulation::kCumulative);

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 0.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, 1.0F);

    SendKeyEvent(Key::kW, ButtonState::kReleased);
    SendKeyEvent(Key::kS, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 0.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, -1.0F);
  });
}

//! Cumulative movement: diagonals combine and opposing keys cancel; releasing
//! everything returns to rest.
NOLINT_TEST_F(BindingScenario, CumulativeWasdCombinesAndCancels)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("gameplay", 0);
    auto move = MakeWasdMove(*ctx, ActionAccumulation::kCumulative);

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    SendKeyEvent(Key::kD, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 1.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, 1.0F);

    SendKeyEvent(Key::kS, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 1.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, 0.0F) << "W and S cancel";
    EXPECT_TRUE(move->IsOngoing());

    SendKeyEvent(Key::kW, ButtonState::kReleased);
    SendKeyEvent(Key::kS, ButtonState::kReleased);
    SendKeyEvent(Key::kD, ButtonState::kReleased);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 0.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, 0.0F);
    EXPECT_FALSE(move->IsOngoing());
  });
}

//! Releasing a strafe key keeps the forward axis and the activation.
NOLINT_TEST_F(BindingScenario, StrafeReleaseKeepsForwardAxis)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("gameplay", 0);
    auto move = MakeWasdMove(*ctx, ActionAccumulation::kCumulative);

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    co_await Frame();
    SendKeyEvent(Key::kA, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, -1.0F);
    SendKeyEvent(Key::kA, ButtonState::kReleased);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*move).x, 0.0F);
    EXPECT_FLOAT_EQ(Vec(*move).y, 1.0F);
    EXPECT_FALSE(move->WasReleasedThisFrame());
  });
}

//! Highest-absolute (default): two keys for the same direction do not stack.
NOLINT_TEST_F(BindingScenario, HighestAbsoluteDoesNotStackDuplicateBindings)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("gameplay", 0);
    auto highest = MakeAction("MoveHighest", ActionValueType::kAxis2D);
    auto cumulative = MakeAction("MoveCumulative", ActionValueType::kAxis2D);
    cumulative->SetAccumulation(ActionAccumulation::kCumulative);
    BindAxis(*ctx, highest, InputSlots::W, { .x = 0.0F, .y = 1.0F });
    BindAxis(*ctx, highest, InputSlots::UpArrow, { .x = 0.0F, .y = 1.0F });
    BindAxis(*ctx, highest, InputSlots::D, { .x = 1.0F, .y = 0.0F });
    BindAxis(*ctx, cumulative, InputSlots::W, { .x = 0.0F, .y = 1.0F });
    BindAxis(*ctx, cumulative, InputSlots::UpArrow, { .x = 0.0F, .y = 1.0F });

    SendKeyEvent(Key::kW, ButtonState::kPressed);
    SendKeyEvent(Key::kUpArrow, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*highest).y, 1.0F);
    EXPECT_FLOAT_EQ(Vec(*cumulative).y, 2.0F) << "documented trade-off";

    SendKeyEvent(Key::kD, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*highest).x, 1.0F) << "diagonal per component";
    EXPECT_FLOAT_EQ(Vec(*highest).y, 1.0F);
  });
}

//! Bias offsets an absolute binding; a 1D action takes the x component.
NOLINT_TEST_F(BindingScenario, BiasOffsetsAbsoluteBinding)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("gameplay", 0);
    auto throttle = MakeAction("Throttle", ActionValueType::kAxis1D);
    auto mapping
      = Map(*ctx, throttle, InputSlots::Space, Explicit<ActionTriggerDown>());
    mapping->SetScale({ .x = 0.5F, .y = 0.5F });
    mapping->SetBias({ .x = 0.25F, .y = 0.0F });

    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    co_await Frame();
    EXPECT_FLOAT_EQ(throttle->GetValue().GetAs<Axis1D>().x, 0.75F);
  });
}

//! Mouse sensitivity: Mouse X with scale 0.5 halves the delta.
NOLINT_TEST_F(BindingScenario, MouseScaleHalvesDelta)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto ctx = MakeContext("camera", 0);
    auto yaw = MakeAction("Yaw", ActionValueType::kAxis1D);
    auto trigger = Explicit<ActionTriggerDown>();
    trigger->SetActuationThreshold(0.0F);
    Map(*ctx, yaw, InputSlots::MouseX, trigger)
      ->SetScale({ .x = 0.5F, .y = 0.5F });

    constexpr float kMouseDelta = 8.0F;
    SendMouseMotion(kMouseDelta, 0.0F);
    co_await Frame();
    EXPECT_FLOAT_EQ(yaw->GetValue().GetAs<Axis1D>().x, 4.0F);
  });
}

} // namespace
