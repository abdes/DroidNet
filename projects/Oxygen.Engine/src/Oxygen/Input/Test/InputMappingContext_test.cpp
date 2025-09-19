//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Unit tests for InputMappingContext routing and update semantics

#include <memory>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
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
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;
using oxygen::platform::KeyEvent;
using oxygen::platform::kInvalidWindowId;
using oxygen::platform::MouseMotionEvent;
using oxygen::platform::MouseWheelEvent;
using oxygen::platform::WindowIdType;
using oxygen::time::CanonicalDuration;

class InputMappingContextTest : public ::testing::Test {
protected:
  void SetUp() override { InputSlots::Initialize(); }

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

//! MouseXY events must route to MouseX mapping when dx!=0 and to MouseY when
//! dy!=0
NOLINT_TEST_F(InputMappingContextTest, SimilarSlots_RoutesMouseXYToXorY)
{
  // Arrange
  InputMappingContext ctx("ctx");

  auto act_x = std::make_shared<Action>(
    "LookX", oxygen::input::ActionValueType::kAxis1D);
  auto map_x = std::make_shared<InputActionMapping>(act_x, InputSlots::MouseX);
  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeExplicit();
  map_x->AddTrigger(down);
  ctx.AddMapping(map_x);

  auto act_y = std::make_shared<Action>(
    "LookY", oxygen::input::ActionValueType::kAxis1D);
  auto map_y = std::make_shared<InputActionMapping>(act_y, InputSlots::MouseY);
  map_y->AddTrigger(std::make_shared<ActionTriggerDown>());
  ctx.AddMapping(map_y);

  // Act: MouseXY dx only
  const auto ev_dx = MakeMouseMotion(6.0F, 0.0F);
  ctx.HandleInput(InputSlots::MouseXY, ev_dx);
  ctx.Update(CanonicalDuration {});

  // Assert: only X mapping updated; Y remained untouched
  EXPECT_EQ(act_x->GetValue().GetAs<Axis1D>().x, 6.0F);
  EXPECT_FALSE(act_y->IsTriggered());

  // Act: MouseXY dy only
  const auto ev_dy = MakeMouseMotion(0.0F, -3.0F);
  ctx.HandleInput(InputSlots::MouseXY, ev_dy);
  ctx.Update(CanonicalDuration {});
  EXPECT_EQ(act_y->GetValue().GetAs<Axis1D>().x, -3.0F);
}

//! MouseWheelXY routes to directional and individual axes based on dx/dy signs
NOLINT_TEST_F(InputMappingContextTest, SimilarSlots_RoutesMouseWheelVariants)
{
  // Arrange
  InputMappingContext ctx("ctx2");
  auto mk = [](const char* name) {
    return std::make_shared<Action>(
      name, oxygen::input::ActionValueType::kAxis1D);
  };

  auto ax = mk("WheelX");
  auto mx = std::make_shared<InputActionMapping>(ax, InputSlots::MouseWheelX);
  mx->AddTrigger(std::make_shared<ActionTriggerDown>());
  ctx.AddMapping(mx);

  auto aleft = mk("WheelLeft");
  auto mleft
    = std::make_shared<InputActionMapping>(aleft, InputSlots::MouseWheelLeft);
  mleft->AddTrigger(std::make_shared<ActionTriggerDown>());
  ctx.AddMapping(mleft);

  auto ay = mk("WheelY");
  auto my = std::make_shared<InputActionMapping>(ay, InputSlots::MouseWheelY);
  my->AddTrigger(std::make_shared<ActionTriggerDown>());
  ctx.AddMapping(my);

  auto adown = mk("WheelDown");
  auto mdown
    = std::make_shared<InputActionMapping>(adown, InputSlots::MouseWheelDown);
  mdown->AddTrigger(std::make_shared<ActionTriggerDown>());
  ctx.AddMapping(mdown);

  // Act: dx<0, dy>0
  const auto ev = MakeMouseWheel(-2.0F, 1.0F);
  ctx.HandleInput(InputSlots::MouseWheelXY, ev);
  ctx.Update(CanonicalDuration {});

  // Assert: X updated (-2), Left fired, Y updated (1), Down not since dy>0
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, -2.0F);
  EXPECT_TRUE(aleft->IsTriggered());
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, 1.0F);
  EXPECT_FALSE(adown->IsTriggered());
}

//! When an earlier mapping consumes input, later mappings get CancelInput()
NOLINT_TEST_F(InputMappingContextTest, Update_ConsumptionCancelsLaterMappings)
{
  // Arrange
  InputMappingContext ctx("ctx3");

  auto a1
    = std::make_shared<Action>("High", oxygen::input::ActionValueType::kBool);
  a1->SetConsumesInput(true);
  auto m1 = std::make_shared<InputActionMapping>(a1, InputSlots::Space);
  auto t1 = std::make_shared<ActionTriggerPressed>();
  t1->MakeExplicit();
  m1->AddTrigger(t1);
  ctx.AddMapping(m1);

  auto a2
    = std::make_shared<Action>("Low", oxygen::input::ActionValueType::kBool);
  auto m2 = std::make_shared<InputActionMapping>(a2, InputSlots::Space);
  auto t2 = std::make_shared<ActionTriggerPressed>();
  t2->MakeExplicit();
  m2->AddTrigger(t2);
  ctx.AddMapping(m2);

  // Act: route a space press to both mappings
  const KeyEvent key(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key);

  // Update: should return true (consumed) and cancel second mapping
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Assert
  EXPECT_TRUE(consumed);
  EXPECT_TRUE(a1->IsTriggered());
  EXPECT_TRUE(a2->IsCanceled()); // later mapping canceled
}

//! If no mapping consumes input, all mappings can process normally
NOLINT_TEST_F(InputMappingContextTest, Update_NoConsumptionProcessesAll)
{
  // Arrange
  InputMappingContext ctx("ctx4");
  auto mk = [] {
    return std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  };

  auto a1 = mk();
  auto m1 = std::make_shared<InputActionMapping>(a1, InputSlots::Space);
  auto t1 = std::make_shared<ActionTriggerPressed>();
  t1->MakeExplicit();
  m1->AddTrigger(t1);
  ctx.AddMapping(m1);

  auto a2 = mk();
  auto m2 = std::make_shared<InputActionMapping>(a2, InputSlots::Space);
  auto t2 = std::make_shared<ActionTriggerPressed>();
  t2->MakeExplicit();
  m2->AddTrigger(t2);
  ctx.AddMapping(m2);

  // Act
  const KeyEvent key(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key);
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Assert: both triggered, not consumed
  EXPECT_FALSE(consumed);
  EXPECT_TRUE(a1->IsTriggered());
  EXPECT_TRUE(a2->IsTriggered());
}

//! Implicit-only triggers: action triggers only when all implicit triggers fire
NOLINT_TEST_F(InputMappingContextTest, ImplicitOnly_AllMustTrigger)
{
  // Arrange: one implicit Down (immediate) and one implicit Hold (delayed)
  InputMappingContext ctx("ctx_implicit");
  auto act = std::make_shared<Action>(
    "ImplicitAll", oxygen::input::ActionValueType::kBool);
  auto map = std::make_shared<InputActionMapping>(act, InputSlots::Space);

  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeImplicit();
  map->AddTrigger(down);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  hold->SetHoldDurationThreshold(0.1F); // 100 ms
  hold->OneShot(true);
  map->AddTrigger(hold);

  ctx.AddMapping(map);

  // Act: press space (routes through context)
  const KeyEvent key_down(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key_down);

  // First update with small dt: Hold not yet satisfied -> no trigger
  EXPECT_FALSE(ctx.Update(CanonicalDuration { 50ms }));
  EXPECT_FALSE(act->IsTriggered());

  // Next update after threshold while still ongoing -> now triggers
  EXPECT_FALSE(ctx.Update(CanonicalDuration { 60ms }));
  EXPECT_TRUE(act->IsTriggered());
}

//! Implicit-only: if not all implicits are satisfied, no trigger
NOLINT_TEST_F(InputMappingContextTest, ImplicitOnly_NotAll_NoTrigger)
{
  // Arrange: two implicit triggers, only one becomes true within dt
  InputMappingContext ctx("ctx_implicit2");
  auto act = std::make_shared<Action>(
    "ImplicitNo", oxygen::input::ActionValueType::kBool);
  auto map = std::make_shared<InputActionMapping>(act, InputSlots::Space);

  auto down = std::make_shared<ActionTriggerDown>();
  down->MakeImplicit();
  map->AddTrigger(down);

  auto hold = std::make_shared<ActionTriggerHold>();
  hold->MakeImplicit();
  hold->SetHoldDurationThreshold(1.0F); // long
  hold->OneShot(true);
  map->AddTrigger(hold);

  ctx.AddMapping(map);

  // Act: press, update with short dt so hold not met
  const KeyEvent key_down(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key_down);
  EXPECT_FALSE(ctx.Update(CanonicalDuration { 100ms }));

  // Assert: no trigger because not all implicits satisfied
  EXPECT_FALSE(act->IsTriggered());
}

//! Chain trigger across contexts: second mapping requires first action armed
NOLINT_TEST_F(
  InputMappingContextTest, Chain_AcrossContexts_RequiresLinkedAction)
{
  // Arrange: Context A provides Shift Down; Context B requires chain to A +
  // Space press
  InputMappingContext ctxA("ctxA");
  auto act_shift
    = std::make_shared<Action>("ShiftA", oxygen::input::ActionValueType::kBool);
  auto map_shift
    = std::make_shared<InputActionMapping>(act_shift, InputSlots::LeftShift);
  auto trig_shift = std::make_shared<ActionTriggerDown>();
  trig_shift->MakeExplicit();
  map_shift->AddTrigger(trig_shift);
  ctxA.AddMapping(map_shift);

  InputMappingContext ctxB("ctxB");
  auto act_combo
    = std::make_shared<Action>("Combo", oxygen::input::ActionValueType::kBool);
  auto map_combo
    = std::make_shared<InputActionMapping>(act_combo, InputSlots::Space);
  auto trig_press = std::make_shared<ActionTriggerPressed>();
  trig_press->MakeExplicit();
  map_combo->AddTrigger(trig_press);
  auto trig_chain = std::make_shared<ActionTriggerChain>();
  trig_chain->SetLinkedAction(act_shift);
  trig_chain->MakeImplicit();
  map_combo->AddTrigger(trig_chain);
  ctxB.AddMapping(map_combo);

  // Act 1: Space without Shift -> should not trigger
  const KeyEvent space_down(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctxB.HandleInput(InputSlots::Space, space_down);
  ctxB.Update(CanonicalDuration {});
  EXPECT_FALSE(act_combo->IsTriggered());
  // Release to reset 'Pressed' trigger depletion
  const KeyEvent space_up(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctxB.HandleInput(InputSlots::Space, space_up);
  ctxB.Update(CanonicalDuration {});

  // Act 2: Press Shift to arm chain
  const KeyEvent shift_down(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kLeftShift, false),
    ButtonState::kPressed);
  ctxA.HandleInput(InputSlots::LeftShift, shift_down);
  ctxA.Update(CanonicalDuration {});
  EXPECT_TRUE(act_shift->IsTriggered());

  // Give chain a chance to arm on ctxB without local press first
  ctxB.Update(CanonicalDuration {});

  // Act 3: Press Space after Shift -> combo should trigger on this update
  ctxB.HandleInput(InputSlots::Space, space_down);
  ctxB.Update(CanonicalDuration {});
  EXPECT_TRUE(act_combo->IsTriggered());
}

//! Events on non-similar slots must not dispatch to unrelated mappings
NOLINT_TEST_F(InputMappingContextTest, Routing_NonSimilarSlots_NoDispatch)
{
  // Arrange
  InputMappingContext ctx("ctx_non_similar");

  auto act = std::make_shared<Action>(
    "MouseX", oxygen::input::ActionValueType::kAxis1D);
  auto map = std::make_shared<InputActionMapping>(act, InputSlots::MouseX);
  auto trig = std::make_shared<ActionTriggerDown>();
  trig->MakeExplicit();
  map->AddTrigger(trig);
  ctx.AddMapping(map);

  // Act: send a MouseWheelXY event which is not similar to MouseX
  const auto wheel = MakeMouseWheel(2.0F, -1.0F);
  ctx.HandleInput(InputSlots::MouseWheelXY, wheel);
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Assert: mapping not invoked, nothing consumed
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(act->IsTriggered());
}

//! MouseXY with both dx and dy should route to both X and Y mappings
NOLINT_TEST_F(InputMappingContextTest, SimilarSlots_MouseXY_BothAxes)
{
  // Arrange
  InputMappingContext ctx("ctx_xy_both");

  auto act_x = std::make_shared<Action>(
    "LookX", oxygen::input::ActionValueType::kAxis1D);
  auto map_x = std::make_shared<InputActionMapping>(act_x, InputSlots::MouseX);
  auto trig_x = std::make_shared<ActionTriggerDown>();
  trig_x->MakeExplicit();
  map_x->AddTrigger(trig_x);
  ctx.AddMapping(map_x);

  auto act_y = std::make_shared<Action>(
    "LookY", oxygen::input::ActionValueType::kAxis1D);
  auto map_y = std::make_shared<InputActionMapping>(act_y, InputSlots::MouseY);
  auto trig_y = std::make_shared<ActionTriggerDown>();
  trig_y->MakeExplicit();
  map_y->AddTrigger(trig_y);
  ctx.AddMapping(map_y);

  // Act: MouseXY with both components
  const auto ev = MakeMouseMotion(5.0F, -4.0F);
  ctx.HandleInput(InputSlots::MouseXY, ev);
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Assert: both mappings updated; context did not consume
  EXPECT_FALSE(consumed);
  EXPECT_EQ(act_x->GetValue().GetAs<Axis1D>().x, 5.0F);
  EXPECT_EQ(act_y->GetValue().GetAs<Axis1D>().x, -4.0F);
  EXPECT_TRUE(act_x->IsTriggered());
  EXPECT_TRUE(act_y->IsTriggered());
}

//! ConsumesInput only applies when the consuming mapping actually triggers
NOLINT_TEST_F(InputMappingContextTest, Consumption_OnlyOnTrigger)
{
  // Arrange
  InputMappingContext ctx("ctx_consume_on_trigger");

  // Mapping 1: consumes input but requires a chain that is not armed -> won't
  // trigger
  auto act_gate
    = std::make_shared<Action>("Gate", oxygen::input::ActionValueType::kBool);
  auto act_consume = std::make_shared<Action>(
    "Consumer", oxygen::input::ActionValueType::kBool);
  act_consume->SetConsumesInput(true);
  auto m1
    = std::make_shared<InputActionMapping>(act_consume, InputSlots::Space);
  auto pressed1 = std::make_shared<ActionTriggerPressed>();
  pressed1->MakeExplicit();
  m1->AddTrigger(pressed1);
  auto chain = std::make_shared<ActionTriggerChain>();
  chain->SetLinkedAction(act_gate); // not armed
  chain->MakeImplicit();
  m1->AddTrigger(chain);
  ctx.AddMapping(m1);

  // Mapping 2: plain pressed on Space, does not consume
  auto act_plain
    = std::make_shared<Action>("Plain", oxygen::input::ActionValueType::kBool);
  auto m2 = std::make_shared<InputActionMapping>(act_plain, InputSlots::Space);
  auto pressed2 = std::make_shared<ActionTriggerPressed>();
  pressed2->MakeExplicit();
  m2->AddTrigger(pressed2);
  ctx.AddMapping(m2);

  // Act: Press Space; m1 should not trigger (chain unmet), m2 should trigger
  const KeyEvent space_down(TimePoint {}, kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, space_down);
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Assert: not consumed because only the non-consuming mapping triggered
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(act_consume->IsTriggered());
  EXPECT_TRUE(act_plain->IsTriggered());
}

} // namespace
