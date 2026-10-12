//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/Test/InputSystemTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

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

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

//! Mouse motion/wheel deltas used to drive axis mappings.
constexpr float kDelta = 3.0F;
constexpr float kLargeDelta = 5.0F;
constexpr float kLookDelta = 6.0F;

//! Mapping context priorities above the default of 0.
constexpr int32_t kMidPriority = 10;
constexpr int32_t kHighPriority = 100;

//! Basic: Space pressed triggers Jump via Pressed trigger
NOLINT_TEST_F(InputSystemTest, ProcessesPressedForJump)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto jump = std::make_shared<Action>("Jump", ActionValueType::kBool);
    System().AddAction(jump);

    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping
      = std::make_shared<InputActionMapping>(jump, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerPressed>();
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Act
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);

    System().OnFrameStart(Context());
    co_await System().OnInput(Context());

    // Assert
    EXPECT_TRUE(jump->IsTriggered());

    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Consumption: first mapping consumes; second mapping does not trigger
NOLINT_TEST_F(InputSystemTest, ConsumptionPreventsSecondMapping)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto primary = std::make_shared<Action>("Primary", ActionValueType::kBool);
    primary->SetConsumesInput(true);
    auto secondary
      = std::make_shared<Action>("Secondary", ActionValueType::kBool);
    System().AddAction(primary);
    System().AddAction(secondary);

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
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Act
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);

    System().OnFrameStart(Context());
    co_await System().OnInput(Context());

    // Assert
    EXPECT_TRUE(primary->IsTriggered());
    EXPECT_FALSE(secondary->IsTriggered());

    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Tap: press and release in the same frame triggers tap
NOLINT_TEST_F(InputSystemTest, TapTriggersOnSameFramePressRelease)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto tap = std::make_shared<Action>("Tap", ActionValueType::kBool);
    System().AddAction(tap);

    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping = std::make_shared<InputActionMapping>(tap, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerTap>();
    trig->SetTapTimeThreshold(0.25F);
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Act: press then release before processing input (same frame)
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);

    System().OnFrameStart(Context());
    co_await System().OnInput(Context());

    // Assert
    EXPECT_TRUE(tap->IsTriggered());

    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Context activation toggle: inactive context should not process input
NOLINT_TEST_F(InputSystemTest, ContextActivationToggle)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto act = std::make_shared<Action>("A", ActionValueType::kBool);
    System().AddAction(act);
    auto ctx = std::make_shared<InputMappingContext>("ctx");
    auto mapping = std::make_shared<InputActionMapping>(act, InputSlots::Space);
    auto trig = std::make_shared<ActionTriggerPressed>();
    trig->MakeExplicit();
    mapping->AddTrigger(trig);
    ctx->AddMapping(mapping);
    System().AddMappingContext(ctx, 0);

    // Inactive: press ignored
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(act->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Activate, press again -> triggers
    System().ActivateMappingContext(ctx);
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_TRUE(act->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Actions and contexts hydrated from assets are identified by asset key:
//! equal names from different assets stay distinct, and adding the same
//! asset context twice keeps one entry.
NOLINT_TEST_F(InputSystemTest, AssetKeyIdentifiesActionsAndContexts)
{
  // Arrange
  using oxygen::data::AssetKey;
  const auto action_a = AssetKey::FromVirtualPath("/Content/A/Jump.oiact");
  const auto action_b = AssetKey::FromVirtualPath("/Content/B/Jump.oiact");
  const auto context_a = AssetKey::FromVirtualPath("/Content/A/Play.oimap");
  const auto context_b = AssetKey::FromVirtualPath("/Content/B/Play.oimap");
  auto jump_a
    = std::make_shared<Action>("Jump", ActionValueType::kBool, action_a);
  auto jump_b
    = std::make_shared<Action>("Jump", ActionValueType::kBool, action_b);
  auto play_a = std::make_shared<InputMappingContext>("Play", context_a);
  auto play_b = std::make_shared<InputMappingContext>("Play", context_b);

  // Act
  System().AddAction(jump_a);
  System().AddAction(jump_b);
  System().AddMappingContext(play_a, 0);
  System().AddMappingContext(play_b, 0);
  System().AddMappingContext(
    std::make_shared<InputMappingContext>("Play", context_a), 0);

  // Assert
  EXPECT_EQ(System().GetActionByKey(action_a), jump_a);
  EXPECT_EQ(System().GetActionByKey(action_b), jump_b);
  EXPECT_EQ(System().GetActionByKey(AssetKey {}), nullptr);
  EXPECT_EQ(System().GetMappingContextByKey(context_a), play_a);
  EXPECT_EQ(System().GetMappingContextByKey(context_b), play_b);
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
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto look_x = std::make_shared<Action>("LookX", ActionValueType::kAxis1D);
    auto look_y = std::make_shared<Action>("LookY", ActionValueType::kAxis1D);
    auto look_xy = std::make_shared<Action>("Look", ActionValueType::kAxis2D);
    System().AddAction(look_x);
    System().AddAction(look_y);
    System().AddAction(look_xy);

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
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Act: dx only
    SendMouseMotion(kLookDelta, 0.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_EQ(look_x->GetValue().GetAs<oxygen::Axis1D>().x, kLookDelta);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: dy only
    SendMouseMotion(0.0F, -kDelta);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_EQ(look_y->GetValue().GetAs<oxygen::Axis1D>().x, -kDelta);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: both
    SendMouseMotion(kLargeDelta, -4.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    const auto v = look_xy->GetValue().GetAs<oxygen::Axis2D>();
    EXPECT_EQ(v.x, kLargeDelta);
    EXPECT_EQ(v.y, -4.0F);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Mouse wheel routing to XY/X/Y directional mappings
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheelToAxisMappings)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto wheel_x = std::make_shared<Action>("WheelX", ActionValueType::kAxis1D);
    auto wheel_y = std::make_shared<Action>("WheelY", ActionValueType::kAxis1D);
    auto wheel_xy = std::make_shared<Action>("Wheel", ActionValueType::kAxis2D);
    System().AddAction(wheel_x);
    System().AddAction(wheel_y);
    System().AddAction(wheel_xy);

    auto ctx = std::make_shared<InputMappingContext>("wheel");
    auto mk_down = []() -> std::shared_ptr<ActionTriggerDown> {
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
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Act: X only
    SendMouseWheel(-2.0F, 0.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_EQ(wheel_x->GetValue().GetAs<oxygen::Axis1D>().x, -2.0F);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: Y only
    SendMouseWheel(0.0F, 1.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_EQ(wheel_y->GetValue().GetAs<oxygen::Axis1D>().x, 1.0F);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: both
    SendMouseWheel(-1.0F, kDelta);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    const auto v = wheel_xy->GetValue().GetAs<oxygen::Axis2D>();
    EXPECT_EQ(v.x, -1.0F);
    EXPECT_EQ(v.y, kDelta);
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Mouse wheel directional slots: Up/Down/Left/Right
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheelDirectionalSlots)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange
    auto up = std::make_shared<Action>("WheelUp", ActionValueType::kBool);
    auto down = std::make_shared<Action>("WheelDown", ActionValueType::kBool);
    auto left = std::make_shared<Action>("WheelLeft", ActionValueType::kBool);
    auto right = std::make_shared<Action>("WheelRight", ActionValueType::kBool);
    System().AddAction(up);
    System().AddAction(down);
    System().AddAction(left);
    System().AddAction(right);

    auto ctx = std::make_shared<InputMappingContext>("wheel-dir");
    auto mk_down = []() -> std::shared_ptr<ActionTriggerDown> {
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
    System().AddMappingContext(ctx, 0);
    System().ActivateMappingContext(ctx);

    // Guard: zero scroll should not trigger any directional actions
    SendMouseWheel(0.0F, 0.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: positive Y scroll -> Up only
    SendMouseWheel(0.0F, 2.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_TRUE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: negative Y scroll -> Down only
    SendMouseWheel(0.0F, -kDelta);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_TRUE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: positive X scroll -> Right only
    SendMouseWheel(4.0F, 0.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_FALSE(left->IsTriggered());
    EXPECT_TRUE(right->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Act: negative X scroll -> Left only
    SendMouseWheel(-kLargeDelta, 0.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(up->IsTriggered());
    EXPECT_FALSE(down->IsTriggered());
    EXPECT_TRUE(left->IsTriggered());
    EXPECT_FALSE(right->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    co_return;
  });
}

//! Mixed scroll (+x, -y): triggers Right and Down across separate contexts
NOLINT_TEST_F(InputSystemTest, RoutesMouseWheel_MixedDirectionalAcrossContexts)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange: two contexts, one mapping Right, the other mapping Down
    auto right = std::make_shared<Action>("WheelRight", ActionValueType::kBool);
    auto down = std::make_shared<Action>("WheelDown", ActionValueType::kBool);
    System().AddAction(right);
    System().AddAction(down);

    auto mk_down = []() -> std::shared_ptr<ActionTriggerDown> {
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
    System().AddMappingContext(ctx_right, kMidPriority);
    System().AddMappingContext(ctx_down, 0);
    System().ActivateMappingContext(ctx_right);
    System().ActivateMappingContext(ctx_down);

    // Act: mixed scroll (+x, -y) should trigger Right and Down independently
    SendMouseWheel(kLargeDelta, -4.0F);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_TRUE(right->IsTriggered());
    EXPECT_TRUE(down->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    co_return;
  });
}

//! Cross-context consumption and staged-input flush behavior
NOLINT_TEST_F(InputSystemTest, CrossContextConsumptionFlushesLowerPriority)
{
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    // Arrange: Two contexts, higher priority consumes on Space, lower has a
    // Pressed on Space
    auto consume = std::make_shared<Action>("Consume", ActionValueType::kBool);
    consume->SetConsumesInput(true);
    auto lower = std::make_shared<Action>("Lower", ActionValueType::kBool);
    System().AddAction(consume);
    System().AddAction(lower);

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
    System().AddMappingContext(low, 0);
    System().AddMappingContext(high, kHighPriority);
    System().ActivateMappingContext(low);
    System().ActivateMappingContext(high);

    // Act: Fire Space; high consumes
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_TRUE(consume->IsTriggered());
    EXPECT_FALSE(lower->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Next frame: Without new events, low must remain idle (flush ensured no
    // staged leak)
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(lower->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

//! Chain+Tap with timing: Shift then Space tap within window triggers SuperJump
NOLINT_TEST_F(InputSystemTest, ChainPlusTap_TimingWindow)
{
  using Tag = oxygen::engine::internal::EngineTagFactory;

  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto shift = std::make_shared<Action>("Shift", ActionValueType::kBool);
    auto super = std::make_shared<Action>("Super", ActionValueType::kBool);
    System().AddAction(shift);
    System().AddAction(super);

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
    System().AddMappingContext(ctxA, 0);
    System().AddMappingContext(ctxB, kHighPriority);
    System().ActivateMappingContext(ctxA);
    System().ActivateMappingContext(ctxB);

    // Act: Shift press, then press+release Space within the same frame
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);

    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_TRUE(super->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Negative: release Space after long delay (> window) should not trigger
    // (simulate time via multiple frames)
    // Hold shift ongoing
    SendKeyEvent(Key::kLeftShift, ButtonState::kPressed);
    // Provide module timing to accumulate enough delta time beyond tap window
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 200ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 200ms };
      Context()->SetModuleTimingData(timing, Tag::Get());
    }
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());

    // Space press, then process two long frames, then release
    SendKeyEvent(Key::kSpace, ButtonState::kPressed);
    // Long frame 1
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 200ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 200ms };
      Context()->SetModuleTimingData(timing, Tag::Get());
    }
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    // Long frame 2
    {
      oxygen::engine::ModuleTimingData timing;
      timing.game_delta_time = oxygen::time::CanonicalDuration { 250ms };
      timing.fixed_delta_time = oxygen::time::CanonicalDuration { 250ms };
      Context()->SetModuleTimingData(timing, Tag::Get());
    }
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    // Now release (too late for 0.25s threshold given frames processed)
    SendKeyEvent(Key::kSpace, ButtonState::kReleased);
    System().OnFrameStart(Context());
    co_await System().OnInput(Context());
    EXPECT_FALSE(super->IsTriggered());
    System().OnSnapshot(Context());
    System().OnFrameEnd(Context());
    co_return;
  });
}

} // namespace
