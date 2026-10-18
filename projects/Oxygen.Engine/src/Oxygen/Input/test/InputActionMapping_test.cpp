//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Unit tests for InputActionMapping behavior (event handling and trigger eval)

#include <chrono>
#include <memory>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/test/ActionTestAccess.h>
#include <Oxygen/Input/test/InputSystemTest.h>
#include <Oxygen/Input/test/InputTestOwner.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::MakeCodeAction;
using oxygen::input::testing::kTestInputOwner;

using namespace std::chrono_literals;

namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::Action;
using oxygen::input::ActionTrigger;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::InputActionMapping;
using oxygen::input::testing::ActionTestAccess;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;
using oxygen::platform::KeyEvent;
using oxygen::platform::kInvalidWindowId;
using oxygen::platform::MouseButton;
using oxygen::platform::MouseButtonEvent;
using oxygen::platform::MouseMotionEvent;
using oxygen::platform::MouseWheelEvent;
using oxygen::platform::WindowIdType;
using oxygen::time::CanonicalDuration;

//! Mouse motion/wheel deltas used to drive axis mappings.
constexpr float kDelta = 3.0F;
constexpr float kLargeDelta = 5.0F;

class InputActionMappingTest : public oxygen::input::testing::InputSystemTest {
protected:
  void SetUp() override { InputSlots::Initialize(); }

  // Helpers to craft events
  auto MakeKey(ButtonState s) -> KeyEvent
  {
    return { Now(), kInvalidWindowId,
      oxygen::platform::input::KeyInfo(Key::kSpace, false), s };
  }
  auto MakeMouseBtn(MouseButton b, ButtonState s) -> MouseButtonEvent
  {
    return MouseButtonEvent(Now(), kInvalidWindowId, { .x = 0, .y = 0 }, b, s);
  }
  auto MakeMouseMotion(float dx, float dy) -> MouseMotionEvent
  {
    return MouseMotionEvent(
      Now(), kInvalidWindowId, { .x = 0, .y = 0 }, { .dx = dx, .dy = dy });
  }
  auto MakeMouseWheel(float dx, float dy) -> MouseWheelEvent
  {
    return MouseWheelEvent(
      Now(), kInvalidWindowId, { .x = 0, .y = 0 }, { .dx = dx, .dy = dy });
  }
};

//! Pressed trigger: key press should trigger once, release should not retrigger
NOLINT_TEST_F(InputActionMappingTest, KeyPressed_TriggersOnce)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Jump", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerPressed>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: press -> update
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  const bool consumed1 = mapping.Update(CanonicalDuration {});

  // Assert
  EXPECT_TRUE(action->IsTriggered());
  EXPECT_FALSE(action->IsOngoing());
  EXPECT_TRUE(consumed1 == action->ConsumesInput());

  // Act: release -> update
  mapping.HandleInput(MakeKey(ButtonState::kReleased));
  const bool consumed2 = mapping.Update(CanonicalDuration {});

  // Assert: no trigger on release for Pressed
  EXPECT_FALSE(action->IsTriggered());
  EXPECT_FALSE(consumed2);
}

//! Down trigger: holding button should keep ongoing true; release ends eval
NOLINT_TEST_F(InputActionMappingTest, KeyDown_OngoingWhileHeld)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Fire", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::LeftMouseButton);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: press -> update
  mapping.HandleInput(MakeMouseBtn(MouseButton::kLeft, ButtonState::kPressed));
  EXPECT_EQ(mapping.Update(CanonicalDuration {}), action->ConsumesInput());

  // Assert
  EXPECT_TRUE(action->IsTriggered());
  EXPECT_TRUE(action->IsOngoing());

  // Act: no new events, continue holding; update with dt
  EXPECT_EQ(
    mapping.Update(CanonicalDuration { 16ms }), action->ConsumesInput());

  // Assert: still ongoing; trigger is per-update based on trigger behavior
  EXPECT_TRUE(action->IsOngoing());

  // Release and update
  mapping.HandleInput(MakeMouseBtn(MouseButton::kLeft, ButtonState::kReleased));
  EXPECT_EQ(mapping.Update(CanonicalDuration {}), action->ConsumesInput());

  // Assert: evaluation ended (mapping stops ongoing)
  EXPECT_FALSE(action->IsOngoing());
}

//! Mouse motion respects mapping slot: MouseX only uses dx, MouseY uses dy
NOLINT_TEST_F(InputActionMappingTest, MouseMotion_UsesMappedAxis)
{
  // Arrange X
  auto ax = MakeCodeAction(
    kTestInputOwner, "LookX", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_x(ax, InputSlots::MouseX);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  map_x.AddTrigger(trig);

  // Act: motion dx=5, dy=0
  map_x.HandleInput(MakeMouseMotion(kLargeDelta, 0.0F));
  EXPECT_EQ(map_x.Update(CanonicalDuration {}), ax->ConsumesInput());

  // Assert: axis1D should be 5
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, kLargeDelta);

  // Arrange Y
  auto ay = MakeCodeAction(
    kTestInputOwner, "LookY", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_y(ay, InputSlots::MouseY);
  map_y.AddTrigger(std::make_shared<ActionTriggerDown>());

  // Act: motion dx=0, dy=-3
  map_y.HandleInput(MakeMouseMotion(0.0F, -kDelta));
  EXPECT_EQ(map_y.Update(CanonicalDuration {}), ay->ConsumesInput());
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, -kDelta);

  // Arrange XY
  auto axy = MakeCodeAction(
    kTestInputOwner, "LookXY", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseXY);
  map_xy.AddTrigger(std::make_shared<ActionTriggerDown>());

  map_xy.HandleInput(MakeMouseMotion(2.0F, 4.0F));
  EXPECT_EQ(map_xy.Update(CanonicalDuration {}), axy->ConsumesInput());
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 2.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, 4.0F);
}

//! Mouse wheel mapping: X/Y/XY and directional slots populate the right axis
NOLINT_TEST_F(InputActionMappingTest, MouseWheel_RespectsSlot)
{
  // X
  auto ax = MakeCodeAction(
    kTestInputOwner, "WheelX", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_x(ax, InputSlots::MouseWheelX);
  map_x.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_x.HandleInput(MakeMouseWheel(-1.0F, 0.0F));
  EXPECT_EQ(map_x.Update(CanonicalDuration {}), ax->ConsumesInput());
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, -1.0F);

  // Y (positive up)
  auto ay = MakeCodeAction(
    kTestInputOwner, "WheelY", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_y(ay, InputSlots::MouseWheelY);
  map_y.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_y.HandleInput(MakeMouseWheel(0.0F, 2.0F));
  EXPECT_EQ(map_y.Update(CanonicalDuration {}), ay->ConsumesInput());
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, 2.0F);

  // XY
  auto axy = MakeCodeAction(
    kTestInputOwner, "WheelXY", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseWheelXY);
  map_xy.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_xy.HandleInput(MakeMouseWheel(kDelta, -4.0F));
  EXPECT_EQ(map_xy.Update(CanonicalDuration {}), axy->ConsumesInput());
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, kDelta);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, -4.0F);
}

//! Blocker trigger prevents any explicit trigger from causing action
NOLINT_TEST_F(InputActionMappingTest, Blocker_PreventsTrigger)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Shoot", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto explicitTrig = std::make_shared<ActionTriggerDown>();
  explicitTrig->MakeExplicit();
  mapping.AddTrigger(explicitTrig);

  // A simple blocker: use Pressed but invert behavior to Blocker
  auto blocker = std::make_shared<ActionTriggerPressed>();
  blocker->MakeBlocker();
  mapping.AddTrigger(blocker);

  // Press space -> both evaluate, but blocker triggers too -> suppress
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  const bool consumed = mapping.Update(CanonicalDuration {});
  EXPECT_FALSE(action->IsTriggered());
  EXPECT_FALSE(consumed);
}

//! CancelInput should mark action canceled in current evaluation
NOLINT_TEST_F(InputActionMappingTest, CancelInput_SetsCanceled)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Interact", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Start with a press
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  EXPECT_EQ(mapping.Update(CanonicalDuration {}), action->ConsumesInput());
  EXPECT_TRUE(action->IsTriggered());

  // Now simulate higher-priority consumption: cancel this mapping
  mapping.CancelInput();
  // After cancel, mapping should end current evaluation; action has canceled
  // edge
  EXPECT_TRUE(action->IsCanceled());
}

//! Mapping with only implicit triggers should trigger when all implicits fire
NOLINT_TEST_F(InputActionMappingTest, ImplicitOnly_AllMustTrigger_Mapping)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Gate", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeImplicit();
  mapping.AddTrigger(down);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  constexpr float kHoldSeconds = 0.05F;
  hold->SetHoldDurationThreshold(kHoldSeconds);
  hold->OneShot(true);
  mapping.AddTrigger(hold);

  // Act: press to actuate Down and start Hold
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  // First small update: hold threshold not met -> no trigger
  EXPECT_FALSE(mapping.Update(CanonicalDuration { 30ms }));
  EXPECT_FALSE(action->IsTriggered());

  // Next update crosses hold threshold while still ongoing
  EXPECT_FALSE(mapping.Update(CanonicalDuration { 30ms }));
  EXPECT_TRUE(action->IsTriggered());
}

//! Blocker should suppress even when implicits are satisfied
NOLINT_TEST_F(InputActionMappingTest, ImplicitWithBlocker_Blocked)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Safe", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeImplicit();
  mapping.AddTrigger(down);

  auto blocker = std::make_shared<ActionTriggerPressed>();
  blocker->MakeBlocker();
  mapping.AddTrigger(blocker);

  // Act: press space -> implicit actuated and blocker triggers
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  const bool consumed = mapping.Update(CanonicalDuration {});

  // Assert
  EXPECT_FALSE(action->IsTriggered());
  EXPECT_FALSE(consumed);
}

//! Mouse motion value clears to zero after update (non-sticky)
NOLINT_TEST_F(InputActionMappingTest, MouseMotion_ValueClearsNextUpdate)
{
  // Arrange
  auto axy = MakeCodeAction(
    kTestInputOwner, "Look", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseXY);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  map_xy.AddTrigger(trig);

  // Act: feed a motion and update
  map_xy.HandleInput(MakeMouseMotion(1.0F, -2.0F));
  EXPECT_EQ(map_xy.Update(CanonicalDuration {}), axy->ConsumesInput());
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 1.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, -2.0F);

  // Relative axes hold the frame's accumulated motion until the next frame
  // starts; a new frame with no motion reads zero.
  ActionTestAccess::BeginFrame(*axy);
  EXPECT_EQ(map_xy.Update(CanonicalDuration {}), axy->ConsumesInput());
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 0.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, 0.0F);
}

//! Pressed trigger should not auto-repeat without a new press event
NOLINT_TEST_F(InputActionMappingTest, Pressed_NoRepeatWithoutEvent)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Click", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerPressed>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: initial press -> triggers
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  EXPECT_EQ(mapping.Update(CanonicalDuration {}), action->ConsumesInput());
  EXPECT_TRUE(action->IsTriggered());

  // Next update with no new event -> must not trigger again
  const bool consumed = mapping.Update(CanonicalDuration { 16ms });
  EXPECT_FALSE(consumed);
  // Action edge flags persist within the same frame; no new trigger emitted
  EXPECT_TRUE(action->IsTriggered());
}

//! Mouse wheel directional specific slots honor sign
NOLINT_TEST_F(InputActionMappingTest, WheelDirectional_SpecificSlots)
{
  // Left (dx < 0)
  auto aleft = MakeCodeAction(
    kTestInputOwner, "Left", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping mleft(aleft, InputSlots::MouseWheelLeft);
  mleft.AddTrigger(std::make_shared<ActionTriggerDown>());
  mleft.HandleInput(MakeMouseWheel(-2.0F, 0.0F));
  EXPECT_EQ(mleft.Update(CanonicalDuration {}), aleft->ConsumesInput());
  EXPECT_LT(aleft->GetValue().GetAs<Axis1D>().x, 0.0F);

  // Up (dy > 0)
  auto aup = MakeCodeAction(
    kTestInputOwner, "Up", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping mup(aup, InputSlots::MouseWheelUp);
  mup.AddTrigger(std::make_shared<ActionTriggerDown>());
  mup.HandleInput(MakeMouseWheel(0.0F, kDelta));
  EXPECT_EQ(mup.Update(CanonicalDuration {}), aup->ConsumesInput());
  EXPECT_GT(aup->GetValue().GetAs<Axis1D>().x, 0.0F);
}

//! Update should consume input only when action consumes and a trigger fired
NOLINT_TEST_F(InputActionMappingTest, ConsumesInput_True_ConsumesOnTrigger)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Use", oxygen::input::ActionValueType::kBool);
  action->SetConsumesInput(true);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerPressed>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: press -> should trigger and consume
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  const bool consumed1 = mapping.Update(CanonicalDuration {});

  // Assert
  EXPECT_TRUE(action->IsTriggered());
  EXPECT_TRUE(consumed1);

  // Act: next update with no new input should not consume again
  const bool consumed2 = mapping.Update(CanonicalDuration { 10ms });

  // Assert
  EXPECT_FALSE(consumed2);
}

//! Mappings without triggers must not react to inputs
NOLINT_TEST_F(InputActionMappingTest, NoTriggers_NoEffect)
{
  // Arrange
  auto action = MakeCodeAction(
    kTestInputOwner, "Noop", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  // Act: press -> update
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  const bool consumed = mapping.Update(CanonicalDuration {});

  // Assert: no triggers -> no effect
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(action->IsTriggered());
  EXPECT_FALSE(action->IsOngoing());
}

//! With explicit present, implicits alone cannot trigger on a later update
NOLINT_TEST_F(
  InputActionMappingTest, ExplicitAndImplicit_RequiresExplicitEachUpdate)
{
  // Arrange: explicit Pressed + implicit Hold; press once then wait long
  auto action = MakeCodeAction(
    kTestInputOwner, "Gated", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  auto pressed = std::make_shared<ActionTriggerPressed>();
  pressed->MakeExplicit();
  mapping.AddTrigger(pressed);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  constexpr float kHoldSeconds = 0.05F;
  hold->SetHoldDurationThreshold(kHoldSeconds);
  hold->OneShot(true);
  mapping.AddTrigger(hold);

  // Act 1: press -> short update (hold not yet satisfied) -> no trigger
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  EXPECT_FALSE(mapping.Update(CanonicalDuration { 20ms }));
  EXPECT_FALSE(action->IsTriggered());

  // Act 2: next update crosses hold threshold but without a fresh press
  // Since an explicit exists, mapping requires explicit firing on this update
  // as well; therefore it must not trigger.
  EXPECT_FALSE(mapping.Update(CanonicalDuration { 40ms }));
  EXPECT_FALSE(action->IsTriggered());
}

//! Scale and bias: value * scale + bias per component; a key feeds both
//! components.
NOLINT_TEST_F(InputActionMappingTest, ScaleAndBias_ApplyPerComponent)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Move", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping mapping(action, InputSlots::Space);
  mapping.AddTrigger(std::make_shared<ActionTriggerDown>());
  mapping.SetScale({ .x = 0.0F, .y = -1.0F });
  mapping.SetBias({ .x = 0.25F, .y = 0.0F });
  EXPECT_EQ(mapping.GetScale().y, -1.0F);
  EXPECT_EQ(mapping.GetBias().x, 0.25F);

  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  (void)mapping.Update(CanonicalDuration {});

  const auto value = action->GetValue().GetAs<Axis2D>();
  EXPECT_FLOAT_EQ(value.x, 0.25F);
  EXPECT_FLOAT_EQ(value.y, -1.0F);
}

//! Bias is not applied to relative sources: a delta has no rest offset.
NOLINT_TEST_F(InputActionMappingTest, Bias_IgnoredForRelativeSources)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Yaw", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping mapping(action, InputSlots::MouseX);
  mapping.AddTrigger(std::make_shared<ActionTriggerDown>());
  mapping.SetScale({ .x = 2.0F, .y = 2.0F });
  mapping.SetBias({ .x = 1.0F, .y = 1.0F });
  EXPECT_TRUE(mapping.IsRelative());

  mapping.HandleInput(MakeMouseMotion(kDelta, 0.0F));
  (void)mapping.Update(CanonicalDuration {});
  EXPECT_FLOAT_EQ(action->GetValue().GetAs<Axis1D>().x, 6.0F);
}

//! Mouse axes and wheel slots are relative; keys and buttons are not.
NOLINT_TEST_F(InputActionMappingTest, IsRelative_ClassifiesSlots)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Any", oxygen::input::ActionValueType::kAxis2D);
  EXPECT_TRUE(InputActionMapping(action, InputSlots::MouseXY).IsRelative());
  EXPECT_TRUE(InputActionMapping(action, InputSlots::MouseWheelY).IsRelative());
  EXPECT_TRUE(
    InputActionMapping(action, InputSlots::MouseWheelUp).IsRelative());
  EXPECT_FALSE(InputActionMapping(action, InputSlots::Space).IsRelative());
  EXPECT_FALSE(
    InputActionMapping(action, InputSlots::LeftMouseButton).IsRelative());
}

//! Several motion events between updates of the same frame accumulate.
NOLINT_TEST_F(InputActionMappingTest, RelativeSource_AccumulatesWithinFrame)
{
  auto action = MakeCodeAction(
    kTestInputOwner, "Look", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping mapping(action, InputSlots::MouseXY);
  auto trigger = std::make_shared<ActionTriggerDown>();
  trigger->SetActuationThreshold(0.0F);
  mapping.AddTrigger(trigger);

  ActionTestAccess::BeginFrame(*action);
  for (int i = 0; i < 3; ++i) {
    mapping.HandleInput(MakeMouseMotion(1.0F, -2.0F));
    (void)mapping.Update(CanonicalDuration {});
  }
  EXPECT_FLOAT_EQ(action->GetValue().GetAs<Axis2D>().x, kDelta);
  EXPECT_FLOAT_EQ(action->GetValue().GetAs<Axis2D>().y, -6.0F);
  ActionTestAccess::EndFrame(*action);

  // A new frame starts a new sum.
  ActionTestAccess::BeginFrame(*action);
  mapping.HandleInput(MakeMouseMotion(1.0F, 0.0F));
  (void)mapping.Update(CanonicalDuration {});
  EXPECT_FLOAT_EQ(action->GetValue().GetAs<Axis2D>().x, 1.0F);
}

//! Deactivate cancels an active binding and leaves an idle one silent.
NOLINT_TEST_F(InputActionMappingTest, Deactivate_CancelsOnlyActiveBinding)
{
  auto held = MakeCodeAction(
    kTestInputOwner, "Sprint", oxygen::input::ActionValueType::kBool);
  InputActionMapping held_mapping(held, InputSlots::Space);
  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeExplicit();
  held_mapping.AddTrigger(down);
  held_mapping.HandleInput(MakeKey(ButtonState::kPressed));
  (void)held_mapping.Update(CanonicalDuration {});
  ASSERT_TRUE(held_mapping.IsActive());

  held_mapping.Deactivate();
  EXPECT_FALSE(held_mapping.IsActive());
  EXPECT_FALSE(held->IsOngoing());
  EXPECT_TRUE(held->WasCanceledThisFrame());

  auto idle = MakeCodeAction(
    kTestInputOwner, "Jump", oxygen::input::ActionValueType::kBool);
  InputActionMapping idle_mapping(idle, InputSlots::Space);
  idle_mapping.AddTrigger(std::make_shared<ActionTriggerPressed>());
  idle_mapping.Deactivate();
  EXPECT_FALSE(idle->WasCanceledThisFrame());
}

} // namespace
