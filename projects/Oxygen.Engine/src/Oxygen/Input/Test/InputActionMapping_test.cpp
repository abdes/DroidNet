//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Unit tests for InputActionMapping behavior (event handling and trigger eval)

#include <memory>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;

namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::TimePoint;
using oxygen::input::Action;
using oxygen::input::ActionTrigger;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::InputActionMapping;
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

class InputActionMappingTest : public ::testing::Test {
protected:
  void SetUp() override { InputSlots::Initialize(); }

  // Helpers to craft events
  static auto MakeKey(ButtonState s) -> KeyEvent
  {
    return KeyEvent(TimePoint {}, kInvalidWindowId,
      oxygen::platform::input::KeyInfo(Key::kSpace, false), s);
  }
  static auto MakeMouseBtn(MouseButton b, ButtonState s) -> MouseButtonEvent
  {
    return MouseButtonEvent(TimePoint {}, kInvalidWindowId, { 0, 0 }, b, s);
  }
  static auto MakeMouseMotion(float dx, float dy) -> MouseMotionEvent
  {
    return MouseMotionEvent(
      TimePoint {}, kInvalidWindowId, { 0, 0 }, { dx, dy });
  }
  static auto MakeMouseWheel(float dx, float dy) -> MouseWheelEvent
  {
    return MouseWheelEvent(
      TimePoint {}, kInvalidWindowId, { 0, 0 }, { dx, dy });
  }
};

//! Pressed trigger: key press should trigger once, release should not retrigger
NOLINT_TEST_F(InputActionMappingTest, KeyPressed_TriggersOnce)
{
  // Arrange
  auto action
    = std::make_shared<Action>("Jump", oxygen::input::ActionValueType::kBool);
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
  auto action
    = std::make_shared<Action>("Fire", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::LeftMouseButton);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: press -> update
  mapping.HandleInput(MakeMouseBtn(MouseButton::kLeft, ButtonState::kPressed));
  mapping.Update(CanonicalDuration {});

  // Assert
  EXPECT_TRUE(action->IsTriggered());
  EXPECT_TRUE(action->IsOngoing());

  // Act: no new events, continue holding; update with dt
  mapping.Update(CanonicalDuration { 16ms });

  // Assert: still ongoing; trigger is per-update based on trigger behavior
  EXPECT_TRUE(action->IsOngoing());

  // Release and update
  mapping.HandleInput(MakeMouseBtn(MouseButton::kLeft, ButtonState::kReleased));
  mapping.Update(CanonicalDuration {});

  // Assert: evaluation ended (mapping stops ongoing)
  EXPECT_FALSE(action->IsOngoing());
}

//! Mouse motion respects mapping slot: MouseX only uses dx, MouseY uses dy
NOLINT_TEST_F(InputActionMappingTest, MouseMotion_UsesMappedAxis)
{
  // Arrange X
  auto ax = std::make_shared<Action>(
    "LookX", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_x(ax, InputSlots::MouseX);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  map_x.AddTrigger(trig);

  // Act: motion dx=5, dy=0
  map_x.HandleInput(MakeMouseMotion(5.0F, 0.0F));
  map_x.Update(CanonicalDuration {});

  // Assert: axis1D should be 5
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, 5.0F);

  // Arrange Y
  auto ay = std::make_shared<Action>(
    "LookY", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_y(ay, InputSlots::MouseY);
  map_y.AddTrigger(std::make_shared<ActionTriggerDown>());

  // Act: motion dx=0, dy=-3
  map_y.HandleInput(MakeMouseMotion(0.0F, -3.0F));
  map_y.Update(CanonicalDuration {});
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, -3.0F);

  // Arrange XY
  auto axy = std::make_shared<Action>(
    "LookXY", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseXY);
  map_xy.AddTrigger(std::make_shared<ActionTriggerDown>());

  map_xy.HandleInput(MakeMouseMotion(2.0F, 4.0F));
  map_xy.Update(CanonicalDuration {});
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 2.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, 4.0F);
}

//! Mouse wheel mapping: X/Y/XY and directional slots populate the right axis
NOLINT_TEST_F(InputActionMappingTest, MouseWheel_RespectsSlot)
{
  // X
  auto ax = std::make_shared<Action>(
    "WheelX", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_x(ax, InputSlots::MouseWheelX);
  map_x.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_x.HandleInput(MakeMouseWheel(-1.0F, 0.0F));
  map_x.Update(CanonicalDuration {});
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, -1.0F);

  // Y (positive up)
  auto ay = std::make_shared<Action>(
    "WheelY", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping map_y(ay, InputSlots::MouseWheelY);
  map_y.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_y.HandleInput(MakeMouseWheel(0.0F, 2.0F));
  map_y.Update(CanonicalDuration {});
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, 2.0F);

  // XY
  auto axy = std::make_shared<Action>(
    "WheelXY", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseWheelXY);
  map_xy.AddTrigger(std::make_shared<ActionTriggerDown>());
  map_xy.HandleInput(MakeMouseWheel(3.0F, -4.0F));
  map_xy.Update(CanonicalDuration {});
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 3.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, -4.0F);
}

//! Blocker trigger prevents any explicit trigger from causing action
NOLINT_TEST_F(InputActionMappingTest, Blocker_PreventsTrigger)
{
  auto action
    = std::make_shared<Action>("Shoot", oxygen::input::ActionValueType::kBool);
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
  auto action = std::make_shared<Action>(
    "Interact", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Start with a press
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  mapping.Update(CanonicalDuration {});
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
  auto action
    = std::make_shared<Action>("Gate", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeImplicit();
  mapping.AddTrigger(down);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  hold->SetHoldDurationThreshold(0.05F);
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
  auto action
    = std::make_shared<Action>("Safe", oxygen::input::ActionValueType::kBool);
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
  auto axy
    = std::make_shared<Action>("Look", oxygen::input::ActionValueType::kAxis2D);
  InputActionMapping map_xy(axy, InputSlots::MouseXY);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  map_xy.AddTrigger(trig);

  // Act: feed a motion and update
  map_xy.HandleInput(MakeMouseMotion(1.0F, -2.0F));
  map_xy.Update(CanonicalDuration {});
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 1.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, -2.0F);

  // Next update with no motion should clear to zero
  map_xy.Update(CanonicalDuration {});
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().x, 0.0F);
  EXPECT_EQ(axy->GetValue().GetAs<Axis2D>().y, 0.0F);
}

//! Pressed trigger should not auto-repeat without a new press event
NOLINT_TEST_F(InputActionMappingTest, Pressed_NoRepeatWithoutEvent)
{
  // Arrange
  auto action
    = std::make_shared<Action>("Click", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);
  auto trig = std::make_shared<ActionTriggerPressed>();
  trig->MakeExplicit();
  mapping.AddTrigger(trig);

  // Act: initial press -> triggers
  mapping.HandleInput(MakeKey(ButtonState::kPressed));
  mapping.Update(CanonicalDuration {});
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
  auto aleft
    = std::make_shared<Action>("Left", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping mleft(aleft, InputSlots::MouseWheelLeft);
  mleft.AddTrigger(std::make_shared<ActionTriggerDown>());
  mleft.HandleInput(MakeMouseWheel(-2.0F, 0.0F));
  mleft.Update(CanonicalDuration {});
  EXPECT_LT(aleft->GetValue().GetAs<Axis1D>().x, 0.0F);

  // Up (dy > 0)
  auto aup
    = std::make_shared<Action>("Up", oxygen::input::ActionValueType::kAxis1D);
  InputActionMapping mup(aup, InputSlots::MouseWheelUp);
  mup.AddTrigger(std::make_shared<ActionTriggerDown>());
  mup.HandleInput(MakeMouseWheel(0.0F, 3.0F));
  mup.Update(CanonicalDuration {});
  EXPECT_GT(aup->GetValue().GetAs<Axis1D>().x, 0.0F);
}

//! Update should consume input only when action consumes and a trigger fired
NOLINT_TEST_F(InputActionMappingTest, ConsumesInput_True_ConsumesOnTrigger)
{
  // Arrange
  auto action
    = std::make_shared<Action>("Use", oxygen::input::ActionValueType::kBool);
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
  auto action
    = std::make_shared<Action>("Noop", oxygen::input::ActionValueType::kBool);
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
  auto action
    = std::make_shared<Action>("Gated", oxygen::input::ActionValueType::kBool);
  InputActionMapping mapping(action, InputSlots::Space);

  auto pressed = std::make_shared<ActionTriggerPressed>();
  pressed->MakeExplicit();
  mapping.AddTrigger(pressed);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  hold->SetHoldDurationThreshold(0.05F);
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

} // namespace
