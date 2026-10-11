//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Relative axes: a high-rate mouse sends many motion events per frame and the
// camera must receive their sum, independently of the frame rate.

#include <array>
#include <chrono>
#include <memory>

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
using oxygen::input::ActionTriggerChain;
using oxygen::input::ActionTriggerDown;
using oxygen::input::ActionValueType;
using oxygen::input::testing::ScenarioTest;
using oxygen::platform::ButtonState;
using oxygen::platform::InputSlots;
using oxygen::platform::MouseButton;

namespace {

class MouseAxisScenario : public ScenarioTest {
protected:
  //! A Down trigger that fires on any motion.
  static auto AnyMotion()
  {
    auto trigger = Explicit<ActionTriggerDown>();
    trigger->SetActuationThreshold(0.0F);
    return trigger;
  }
};

//! Fly camera (DemoShell layout): look on MouseXY while RMB is held. A
//! 1000 Hz mouse at 60 fps reports 16 motion events per frame.
NOLINT_TEST_F(MouseAxisScenario, LookReceivesWholeFrameDelta)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto rmb = MakeAction("rmb", ActionValueType::kBool);
    auto look = MakeAction("look", ActionValueType::kAxis2D);
    auto ctx = MakeContext("camera fly", 0);
    Map(*ctx, rmb, InputSlots::RightMouseButton, Explicit<ActionTriggerDown>());
    {
      auto chain = std::make_shared<ActionTriggerChain>();
      chain->SetLinkedAction(rmb);
      chain->MakeImplicit();
      chain->RequirePrerequisiteHeld(true);
      Map(*ctx, look, InputSlots::MouseXY, AnyMotion())->AddTrigger(chain);
    }

    SendMouseButtonEvent(MouseButton::kRight, ButtonState::kPressed);
    co_await Frame();
    constexpr int kEventsPerFrame = 16;
    for (int i = 0; i < kEventsPerFrame; ++i) {
      SendMouseMotion(1.0F, 0.5F);
    }
    co_await Frame();

    EXPECT_FLOAT_EQ(Vec(*look).x, 16.0F);
    EXPECT_FLOAT_EQ(Vec(*look).y, 8.0F);

    co_await Frame();
    EXPECT_FLOAT_EQ(Vec(*look).x, 0.0F) << "no motion in this frame";
  });
}

//! The same physical sweep (1000 counts at 1000 Hz) turns the camera by the
//! same amount at any frame rate.
NOLINT_TEST_F(MouseAxisScenario, LookDoesNotDependOnFrameRate)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto look = MakeAction("look", ActionValueType::kAxis2D);
    auto ctx = MakeContext("camera", 0);
    Map(*ctx, look, InputSlots::MouseXY, AnyMotion());

    // The test channel holds 32 events: the lowest rate is 50 fps.
    constexpr std::array kRates { 50, 100, 250 };
    constexpr Duration kOneSecond = std::chrono::seconds { 1 };
    for (const int fps : kRates) {
      constexpr int kCounts = 1000;
      const int per_frame = kCounts / fps;
      float turned = 0.0F;
      for (int sent = 0; sent < kCounts; sent += per_frame) {
        for (int i = 0; i < per_frame; ++i) {
          SendMouseMotion(1.0F, 0.0F);
        }
        co_await Frame(kOneSecond / fps);
        turned += Vec(*look).x;
      }
      co_await Frame();
      EXPECT_FLOAT_EQ(turned, 1000.0F) << "at " << fps << " fps";
    }
  });
}

//! Wheel zoom on MouseWheelY receives a whole-frame scroll, and a
//! MouseWheelXY binding receives single-axis scrolls too.
NOLINT_TEST_F(MouseAxisScenario, WheelAccumulatesAndReachesXYBindings)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  oxygen::co::Run(Loop(), [&]() -> oxygen::co::Co<> {
    auto zoom = MakeAction("zoom", ActionValueType::kAxis1D);
    auto scroll = MakeAction("scroll", ActionValueType::kAxis2D);
    auto ctx = MakeContext("camera", 0);
    Map(*ctx, zoom, InputSlots::MouseWheelY, AnyMotion());
    Map(*ctx, scroll, InputSlots::MouseWheelXY, AnyMotion());

    SendMouseWheel(0.0F, 1.0F);
    SendMouseWheel(0.0F, 1.0F);
    SendMouseWheel(0.0F, 1.0F);
    co_await Frame();
    EXPECT_FLOAT_EQ(zoom->GetValue().GetAs<Axis1D>().x, 3.0F);
    EXPECT_FLOAT_EQ(Vec(*scroll).y, 3.0F);
  });
}

} // namespace
