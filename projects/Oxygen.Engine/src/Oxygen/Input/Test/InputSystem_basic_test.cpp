//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/Test/InputSystemTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>

using namespace std::chrono_literals;

using oxygen::observer_ptr;
using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::testing::InputSystemTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

namespace {

//! Basic: Space pressed triggers Jump via Pressed trigger
NOLINT_TEST_F(InputSystemTest, ProcessesPressedForJump)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
    input_system_->AddAction(jump);

    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping
      = std::make_shared<InputActionMapping>(jump, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerPressed>();
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Act
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);

    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });

    // Assert
    EXPECT_TRUE(jump->IsTriggered());

    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Consumption: first mapping consumes; second mapping does not trigger
NOLINT_TEST_F(InputSystemTest, ConsumptionPreventsSecondMapping)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto primary = std::make_shared<Action>("Primary", ActionValueType::kBool);
    primary->SetConsumesInput(true);
    auto secondary
      = std::make_shared<Action>("Secondary", ActionValueType::kBool);
    input_system_->AddAction(primary);
    input_system_->AddAction(secondary);

    auto ctx = std::make_shared<InputMappingContext>("ctx");
    // Primary first
    {
      auto m = std::make_shared<InputActionMapping>(primary, InputSlots::Space);
      auto t = std::make_shared<ActionTriggerPressed>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }
    // Secondary second
    {
      auto m
        = std::make_shared<InputActionMapping>(secondary, InputSlots::Space);
      auto t = std::make_shared<ActionTriggerPressed>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Act
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);

    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });

    // Assert
    EXPECT_TRUE(primary->IsTriggered());
    EXPECT_FALSE(secondary->IsTriggered());

    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Tap: press and release in the same frame triggers tap
NOLINT_TEST_F(InputSystemTest, TapTriggersOnSameFramePressRelease)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto tap = std::make_shared<Action>("Tap", ActionValueType::kBool);
    input_system_->AddAction(tap);

    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping = std::make_shared<InputActionMapping>(tap, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerTap>();
    trig->SetTapTimeThreshold(0.25F);
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Act: press then release before processing input (same frame)
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);

    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });

    // Assert
    EXPECT_TRUE(tap->IsTriggered());

    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Context activation toggle: inactive context should not process input
NOLINT_TEST_F(InputSystemTest, ContextActivationToggle)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto act = std::make_shared<Action>("A", ActionValueType::kBool);
    input_system_->AddAction(act);
    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping = std::make_shared<InputActionMapping>(act, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerPressed>();
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    input_system_->AddMappingContext(ctx, 0);

    // Inactive: press ignored
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(act->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Activate, press again -> triggers
    input_system_->ActivateMappingContext(ctx);
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_TRUE(act->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

} // namespace

namespace {

using oxygen::input::Action;
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::testing::InputSystemTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::Key;

//! Mouse motion routing to X/Y/XY mappings
NOLINT_TEST_F(InputSystemTest, RoutesMouseMotionToAxisMappings)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto look_x = std::make_shared<Action>("LookX", ActionValueType::kAxis1D);
    auto look_y = std::make_shared<Action>("LookY", ActionValueType::kAxis1D);
    auto look_xy = std::make_shared<Action>("Look", ActionValueType::kAxis2D);
    input_system_->AddAction(look_x);
    input_system_->AddAction(look_y);
    input_system_->AddAction(look_xy);

    auto ctx = std::make_shared<InputMappingContext>("mouse");
    {
      auto m = std::make_shared<InputActionMapping>(look_x, InputSlots::MouseX);
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(look_y, InputSlots::MouseY);
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }
    {
      auto m
        = std::make_shared<InputActionMapping>(look_xy, InputSlots::MouseXY);
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctx->AddMapping(m);
    }
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Act: dx only
    SendMouseMotion(6.0F, 0.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_EQ(look_x->GetValue().GetAs<oxygen::Axis1D>().x, 6.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: dy only
    SendMouseMotion(0.0F, -3.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_EQ(look_y->GetValue().GetAs<oxygen::Axis1D>().x, -3.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: both
    SendMouseMotion(5.0F, -4.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    const auto v = look_xy->GetValue().GetAs<oxygen::Axis2D>();
    EXPECT_EQ(v.x, 5.0F);
    EXPECT_EQ(v.y, -4.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Mouse wheel routing to XY/X/Y directional mappings
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheelToAxisMappings)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto wheel_x = std::make_shared<Action>("WheelX", ActionValueType::kAxis1D);
    auto wheel_y = std::make_shared<Action>("WheelY", ActionValueType::kAxis1D);
    auto wheel_xy = std::make_shared<Action>("Wheel", ActionValueType::kAxis2D);
    input_system_->AddAction(wheel_x);
    input_system_->AddAction(wheel_y);
    input_system_->AddAction(wheel_xy);

    auto ctx = std::make_shared<InputMappingContext>("wheel");
    auto mk_down = [] {
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      return t;
    };
    {
      auto m = std::make_shared<InputActionMapping>(
        wheel_x, InputSlots::MouseWheelX);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(
        wheel_y, InputSlots::MouseWheelY);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(
        wheel_xy, InputSlots::MouseWheelXY);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Act: X only
    SendMouseWheel(-2.0F, 0.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_EQ(wheel_x->GetValue().GetAs<oxygen::Axis1D>().x, -2.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: Y only
    SendMouseWheel(0.0F, 1.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_EQ(wheel_y->GetValue().GetAs<oxygen::Axis1D>().x, 1.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: both
    SendMouseWheel(-1.0F, 3.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    const auto v = wheel_xy->GetValue().GetAs<oxygen::Axis2D>();
    EXPECT_EQ(v.x, -1.0F);
    EXPECT_EQ(v.y, 3.0F);
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Mouse wheel directional slots: Up/Down/Left/Right
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheelDirectionalSlots)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange
    auto up = std::make_shared<Action>("WheelUp", ActionValueType::kBool);
    auto down = std::make_shared<Action>("WheelDown", ActionValueType::kBool);
    auto left = std::make_shared<Action>("WheelLeft", ActionValueType::kBool);
    auto right = std::make_shared<Action>("WheelRight", ActionValueType::kBool);
    input_system_->AddAction(up);
    input_system_->AddAction(down);
    input_system_->AddAction(left);
    input_system_->AddAction(right);

    auto ctx = std::make_shared<InputMappingContext>("wheel-dir");
    auto mk_down = [] {
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      return t;
    };
    {
      auto m
        = std::make_shared<InputActionMapping>(up, InputSlots::MouseWheelUp);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(
        down, InputSlots::MouseWheelDown);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(
        left, InputSlots::MouseWheelLeft);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    {
      auto m = std::make_shared<InputActionMapping>(
        right, InputSlots::MouseWheelRight);
      m->AddTrigger(mk_down());
      ctx->AddMapping(m);
    }
    input_system_->AddMappingContext(ctx, 0);
    input_system_->ActivateMappingContext(ctx);

    // Guard: zero scroll should not trigger any directional actions
    SendMouseWheel(0.0F, 0.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: positive Y scroll -> Up only
    SendMouseWheel(0.0F, 2.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_TRUE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: negative Y scroll -> Down only
    SendMouseWheel(0.0F, -3.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_TRUE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: positive X scroll -> Right only
    SendMouseWheel(4.0F, 0.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_TRUE(right->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Act: negative X scroll -> Left only
    SendMouseWheel(-5.0F, 0.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_TRUE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    co_return;
  });
}

//! Mixed scroll (+x, -y): triggers Right and Down across separate contexts
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheel_MixedDirectionalAcrossContexts)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange: two contexts, one mapping Right, the other mapping Down
    auto right = std::make_shared<Action>("WheelRight", ActionValueType::kBool);
    auto down = std::make_shared<Action>("WheelDown", ActionValueType::kBool);
    input_system_->AddAction(right);
    input_system_->AddAction(down);

    auto mk_down = [] {
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      return t;
    };

    auto ctx_right = std::make_shared<InputMappingContext>("wheel-right");
    {
      auto m = std::make_shared<InputActionMapping>(
        right, InputSlots::MouseWheelRight);
      m->AddTrigger(mk_down());
      ctx_right->AddMapping(m);
    }

    auto ctx_down = std::make_shared<InputMappingContext>("wheel-down");
    {
      auto m = std::make_shared<InputActionMapping>(
        down, InputSlots::MouseWheelDown);
      m->AddTrigger(mk_down());
      ctx_down->AddMapping(m);
    }

    // Add both contexts with different priorities; neither action consumes
    input_system_->AddMappingContext(ctx_right, 10);
    input_system_->AddMappingContext(ctx_down, 0);
    input_system_->ActivateMappingContext(ctx_right);
    input_system_->ActivateMappingContext(ctx_down);

    // Act: mixed scroll (+x, -y) should trigger Right and Down independently
    SendMouseWheel(5.0F, -4.0F);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_TRUE(right->IsTriggered());
    EXPECT_TRUE(down->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    co_return;
  });
}

//! Cross-context consumption and staged-input flush behavior
NOLINT_TEST_F(InputSystemTest, CrossContextConsumptionFlushesLowerPriority)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    // Arrange: Two contexts, higher priority consumes on Space, lower has a
    // Pressed on Space
    auto consume = std::make_shared<Action>("Consume", ActionValueType::kBool);
    consume->SetConsumesInput(true);
    auto lower = std::make_shared<Action>("Lower", ActionValueType::kBool);
    input_system_->AddAction(consume);
    input_system_->AddAction(lower);

    auto high = std::make_shared<InputMappingContext>("high");
    {
      auto m = std::make_shared<InputActionMapping>(consume, InputSlots::Space);
      auto t = std::make_shared<ActionTriggerPressed>();
      t->MakeExplicit();
      m->AddTrigger(t);
      high->AddMapping(m);
    }
    auto low = std::make_shared<InputMappingContext>("low");
    {
      auto m = std::make_shared<InputActionMapping>(lower, InputSlots::Space);
      auto t = std::make_shared<ActionTriggerPressed>();
      t->MakeExplicit();
      m->AddTrigger(t);
      low->AddMapping(m);
    }
    // Higher numeric priority first in reverse view (add low with 0, high with
    // 100)
    input_system_->AddMappingContext(low, 0);
    input_system_->AddMappingContext(high, 100);
    input_system_->ActivateMappingContext(low);
    input_system_->ActivateMappingContext(high);

    // Act: Fire Space; high consumes
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_TRUE(consume->IsTriggered());
    EXPECT_FALSE(lower->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Next frame: Without new events, low must remain idle (flush ensured no
    // staged leak)
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(lower->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

//! Chain+Tap with timing: Shift then Space tap within window triggers SuperJump
NOLINT_TEST_F(InputSystemTest, ChainPlusTap_TimingWindow)
{
  oxygen::co::Run(loop_, [&]() -> oxygen::co::Co<> {
    auto shift = std::make_shared<Action>("Shift", ActionValueType::kBool);
    auto super = std::make_shared<Action>("Super", ActionValueType::kBool);
    input_system_->AddAction(shift);
    input_system_->AddAction(super);

    // Context A: Shift Down explicit
    auto ctxA = std::make_shared<InputMappingContext>("A");
    {
      auto m
        = std::make_shared<InputActionMapping>(shift, InputSlots::LeftShift);
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      m->AddTrigger(t);
      ctxA->AddMapping(m);
    }
    // Context B: Super with Tap explicit + Chain(shift) implicit, and give it
    // higher priority
    auto ctxB = std::make_shared<InputMappingContext>("B");
    {
      auto m = std::make_shared<InputActionMapping>(super, InputSlots::Space);
      auto tap = std::make_shared<ActionTriggerTap>();
      tap->SetTapTimeThreshold(0.25F);
      tap->MakeExplicit();
      m->AddTrigger(tap);
      auto chain = std::make_shared<ActionTriggerChain>();
      chain->SetLinkedAction(shift);
      chain->MakeImplicit();
      m->AddTrigger(chain);
      ctxB->AddMapping(m);
    }
    input_system_->AddMappingContext(ctxA, 0);
    input_system_->AddMappingContext(ctxB, 100);
    input_system_->ActivateMappingContext(ctxA);
    input_system_->ActivateMappingContext(ctxB);

    // Act: Shift press, then press+release Space within the same frame
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);

    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_TRUE(super->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Negative: release Space after long delay (> window) should not trigger
    // (simulate time via multiple frames)
    auto engine_tag = oxygen::engine::internal::EngineTagFactory::Get();
    // Hold shift ongoing
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    // Provide module timing to accumulate enough delta time beyond tap window
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 200ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 200ms };
      frame_context_->SetModuleTimingData(timing, engine_tag);
    }
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });

    // Space press, then process two long frames, then release
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    // Long frame 1
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 200ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 200ms };
      frame_context_->SetModuleTimingData(timing, engine_tag);
    }
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    // Long frame 2
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 250ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 250ms };
      frame_context_->SetModuleTimingData(timing, engine_tag);
    }
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    // Now release (too late for 0.25s threshold given frames processed)
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    input_system_->OnFrameStart(observer_ptr { frame_context_.get() });
    co_await input_system_->OnInput(observer_ptr { frame_context_.get() });
    EXPECT_FALSE(super->IsTriggered());
    input_system_->OnSnapshot(observer_ptr { frame_context_.get() });
    input_system_->OnFrameEnd(observer_ptr { frame_context_.get() });
    co_return;
  });
}

} // namespace
