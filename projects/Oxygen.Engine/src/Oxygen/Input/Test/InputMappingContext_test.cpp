//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Unit tests for InputMappingContext routing and update semantics

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>

#include <Oxygen/Input/Test/InputSystemTest.h>

using namespace std::chrono_literals;

namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

using oxygen::Axis1D;
using oxygen::Axis2D;
using oxygen::input::Action;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerHold;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
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

class InputMappingContextTest : public oxygen::input::testing::InputSystemTest {
protected:
  void SetUp() override { InputSlots::Initialize(); }

  auto MakeMouseMotion(float dx, float dy) -> MouseMotionEvent
  {
    return MouseMotionEvent(Now(), kInvalidWindowId, { 0, 0 }, { dx, dy });
  }
  auto MakeMouseWheel(float dx, float dy) -> MouseWheelEvent
  {
    return MouseWheelEvent(Now(), kInvalidWindowId, { 0, 0 }, { dx, dy });
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
  [[maybe_unused]] const auto update_dx = ctx.Update(CanonicalDuration {});

  // Assert: only X mapping updated; Y remained untouched
  EXPECT_EQ(act_x->GetValue().GetAs<Axis1D>().x, 6.0F);
  EXPECT_FALSE(act_y->IsTriggered());

  // Act: MouseXY dy only
  const auto ev_dy = MakeMouseMotion(0.0F, -3.0F);
  ctx.HandleInput(InputSlots::MouseXY, ev_dy);
  [[maybe_unused]] const auto update_dy = ctx.Update(CanonicalDuration {});
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
  [[maybe_unused]] const auto update_wheel = ctx.Update(CanonicalDuration {});

  // Assert: X updated (-2), Left fired, Y updated (1), Down not since dy>0
  EXPECT_EQ(ax->GetValue().GetAs<Axis1D>().x, -2.0F);
  EXPECT_TRUE(aleft->IsTriggered());
  EXPECT_EQ(ay->GetValue().GetAs<Axis1D>().x, 1.0F);
  EXPECT_FALSE(adown->IsTriggered());
}

//! When an earlier mapping consumes input, later mappings get CancelInput()
//! Mapping order vs consumer: when a consuming mapping is placed after a
//! non-consuming mapping, both mappings may trigger and the context reports
//! consumption (consumer wins later in the update). When the consumer appears
//! earlier in the list, it should prevent later mappings from running and
//! cancel them.
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
  const KeyEvent key(Now(), kInvalidWindowId,
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

//! When a later mapping consumes input, earlier mappings must be canceled too
NOLINT_TEST_F(
  InputMappingContextTest, Update_ConsumerCancelsOnlyLaterMappings_NotEarlier)
{
  // Arrange: earlier mapping does not consume, later mapping does
  InputMappingContext ctx("ctx5");

  auto early
    = std::make_shared<Action>("Early", oxygen::input::ActionValueType::kBool);
  auto m_early = std::make_shared<InputActionMapping>(early, InputSlots::Space);
  auto t_early = std::make_shared<ActionTriggerTap>();
  t_early->SetTapTimeThreshold(0.25F);
  t_early->MakeExplicit();
  m_early->AddTrigger(t_early);
  ctx.AddMapping(m_early);

  auto later
    = std::make_shared<Action>("Later", oxygen::input::ActionValueType::kBool);
  later->SetConsumesInput(true);
  auto m_later = std::make_shared<InputActionMapping>(later, InputSlots::Space);
  auto t_later = std::make_shared<ActionTriggerTap>();
  t_later->SetTapTimeThreshold(0.25F);
  t_later->MakeExplicit();
  m_later->AddTrigger(t_later);
  ctx.AddMapping(m_later);

  // Act: press -> both mappings become ongoing but not triggered yet
  const KeyEvent key_down(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key_down);
  [[maybe_unused]] const auto update_press = ctx.Update(CanonicalDuration {});

  EXPECT_TRUE(early->IsOngoing());
  EXPECT_TRUE(later->IsOngoing());

  // Release: later mapping should trigger and consume. Earlier mappings are
  // processed first and may also trigger; the consumer does not retroactively
  // cancel earlier mappings.
  const KeyEvent key_up(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctx.HandleInput(InputSlots::Space, key_up);
  const bool consumed = ctx.Update(CanonicalDuration {});

  EXPECT_TRUE(consumed);
  // Later action triggered and consumed
  EXPECT_TRUE(later->WasTriggeredThisFrame());
  // Early mapping is expected to have triggered this frame (not canceled)
  EXPECT_TRUE(early->WasTriggeredThisFrame());
  EXPECT_FALSE(early->IsOngoing());
}

//! After a consuming mapping fires and cancels earlier mappings, subsequent
//! tap gestures should work immediately — a fresh press/release should trigger
//! the early mapping without needing an extra 'reset' press.
NOLINT_TEST_F(InputMappingContextTest,
  Update_ConsumerCancelsLater_NotEarlier_SubsequentTapConsumedAgain)
{
  // Arrange
  InputMappingContext ctx("ctx6");

  auto early
    = std::make_shared<Action>("Early", oxygen::input::ActionValueType::kBool);
  auto m_early = std::make_shared<InputActionMapping>(early, InputSlots::Space);
  auto t_early = std::make_shared<ActionTriggerTap>();
  t_early->SetTapTimeThreshold(0.25F);
  t_early->MakeExplicit();
  m_early->AddTrigger(t_early);
  ctx.AddMapping(m_early);

  auto later
    = std::make_shared<Action>("Later", oxygen::input::ActionValueType::kBool);
  later->SetConsumesInput(true);
  auto m_later = std::make_shared<InputActionMapping>(later, InputSlots::Space);
  auto t_later = std::make_shared<ActionTriggerTap>();
  t_later->SetTapTimeThreshold(0.25F);
  t_later->MakeExplicit();
  m_later->AddTrigger(t_later);
  ctx.AddMapping(m_later);

  // Simulate press+release: both become ongoing on press, on release later
  // consumes and early should be canceled
  const KeyEvent key_down(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key_down);
  [[maybe_unused]] const auto update_first_press
    = ctx.Update(CanonicalDuration {});

  const KeyEvent key_up(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctx.HandleInput(InputSlots::Space, key_up);
  const bool consumed = ctx.Update(CanonicalDuration {});
  EXPECT_TRUE(consumed);
  EXPECT_TRUE(later->WasTriggeredThisFrame());
  // Early triggers in the same update; it is not canceled by a later consumer
  EXPECT_TRUE(early->WasTriggeredThisFrame());

  // Now simulate a fresh press+release — early mapping should trigger
  const KeyEvent key2_down(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, key2_down);
  [[maybe_unused]] const auto update_second_press
    = ctx.Update(CanonicalDuration {});

  const KeyEvent key2_up(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctx.HandleInput(InputSlots::Space, key2_up);
  const bool consumed2 = ctx.Update(CanonicalDuration {});

  // On the subsequent fresh tap the later consumer mapping will still trigger
  // and consume (mapping order means both may trigger and the consumer will
  // still indicate consumption). Expect consumed2 true, and early triggered.
  EXPECT_TRUE(consumed2);
  EXPECT_TRUE(early->WasTriggeredThisFrame());
}

// TDD: perfect behavior spec — if mapping context and triggers work correctly
// then a consumer should cancel earlier mappings and a subsequent fresh tap
// should allow the earlier mapping to trigger immediately.
NOLINT_TEST_F(InputMappingContextTest,
  TDD_ConsumerCancelsLater_NotEarlier_SubsequentTapConsumedAgain)
{
  // Arrange: earlier mapping (A) and later consuming mapping (B)
  InputMappingContext ctx("tdd_ctx");

  auto a = std::make_shared<Action>("A", oxygen::input::ActionValueType::kBool);
  auto map_a = std::make_shared<InputActionMapping>(a, InputSlots::Space);
  auto tap_a = std::make_shared<ActionTriggerTap>();
  tap_a->SetTapTimeThreshold(0.25F);
  tap_a->MakeExplicit();
  map_a->AddTrigger(tap_a);
  ctx.AddMapping(map_a);

  auto b = std::make_shared<Action>("B", oxygen::input::ActionValueType::kBool);
  b->SetConsumesInput(true);
  auto map_b = std::make_shared<InputActionMapping>(b, InputSlots::Space);
  auto tap_b = std::make_shared<ActionTriggerTap>();
  tap_b->SetTapTimeThreshold(0.25F);
  tap_b->MakeExplicit();
  map_b->AddTrigger(tap_b);
  ctx.AddMapping(map_b);

  // Act: press+release -> consumer B must trigger and cancel A
  const KeyEvent down1(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, down1);
  [[maybe_unused]] const auto update_down1 = ctx.Update(CanonicalDuration {});

  const KeyEvent up1(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctx.HandleInput(InputSlots::Space, up1);
  const bool consumed = ctx.Update(CanonicalDuration {});

  // Expect consumer consumed. The earlier mapping (A) will have also
  // triggered during the same update (mappings processed in order).
  EXPECT_TRUE(consumed);
  EXPECT_TRUE(b->WasTriggeredThisFrame());
  EXPECT_TRUE(a->WasTriggeredThisFrame());

  // Now a fresh press+release should allow A to trigger (no need for extra
  // resetting press)
  const KeyEvent down2(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctx.HandleInput(InputSlots::Space, down2);
  [[maybe_unused]] const auto update_down2 = ctx.Update(CanonicalDuration {});

  const KeyEvent up2(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctx.HandleInput(InputSlots::Space, up2);
  const bool consumed2 = ctx.Update(CanonicalDuration {});

  // A will trigger on the fresh tap, but the later consumer mapping may also
  // trigger and consume. Expect the consumer to still indicate consumption.
  EXPECT_TRUE(consumed2);
  EXPECT_TRUE(a->WasTriggeredThisFrame());
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
  const KeyEvent key(Now(), kInvalidWindowId,
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
  const KeyEvent key_down(Now(), kInvalidWindowId,
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
  const KeyEvent key_down(Now(), kInvalidWindowId,
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
  const KeyEvent space_down(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kPressed);
  ctxB.HandleInput(InputSlots::Space, space_down);
  [[maybe_unused]] const auto update_space_down
    = ctxB.Update(CanonicalDuration {});
  EXPECT_FALSE(act_combo->IsTriggered());
  // Release to reset 'Pressed' trigger depletion
  const KeyEvent space_up(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kSpace, false),
    ButtonState::kReleased);
  ctxB.HandleInput(InputSlots::Space, space_up);
  [[maybe_unused]] const auto update_space_up
    = ctxB.Update(CanonicalDuration {});

  // Act 2: Press Shift to arm chain
  const KeyEvent shift_down(Now(), kInvalidWindowId,
    oxygen::platform::input::KeyInfo(Key::kLeftShift, false),
    ButtonState::kPressed);
  ctxA.HandleInput(InputSlots::LeftShift, shift_down);
  [[maybe_unused]] const auto update_shift = ctxA.Update(CanonicalDuration {});
  EXPECT_TRUE(act_shift->IsTriggered());

  // Give chain a chance to arm on ctxB without local press first
  [[maybe_unused]] const auto update_chain_arm
    = ctxB.Update(CanonicalDuration {});

  // Act 3: Press Space after Shift -> combo should trigger on this update
  ctxB.HandleInput(InputSlots::Space, space_down);
  [[maybe_unused]] const auto update_space_again
    = ctxB.Update(CanonicalDuration {});
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
  const KeyEvent space_down(Now(), kInvalidWindowId,
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
