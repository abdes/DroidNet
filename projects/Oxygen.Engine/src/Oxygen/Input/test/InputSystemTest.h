//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::input::testing {

//! Reusable fixture for InputSystem unit tests
/*!
 Provides a minimal, self-contained test harness around the asynchronous
 InputSystem pipeline using a BroadcastChannel and a TestEventLoop.

 ### Features
 - Pre-wired BroadcastChannel for feeding platform InputEvents
 - Constructed InputSystem bound to the channel reader
 - FrameContext instance for frame lifecycle calls
 - Helpers to send keyboard/mouse events and advance simulated time

 Intended for unit tests that need to exercise InputSystem behavior without
 duplicating setup code. Derived tests can call OnFrameStart/OnInput/
 OnSnapshot/OnFrameEnd directly on the provided InputSystem.

 ### Coroutine test bodies
 Tests run their body as `co::Run(Loop(), [&]() -> co::Co<> { ... })`. The
 capture is safe: Run() blocks until the coroutine completes, and the lambda
 temporary lives until the end of that full expression, so every captured
 fixture member outlives the coroutine.
*/
class InputSystemTest : public ::testing::Test {
protected:
  void SetUp() override;

  void TearDown() override;

  //! Helper to send a keyboard input event through the broadcast channel
  void SendKeyEvent(
    oxygen::platform::Key key, oxygen::platform::ButtonState state);

  //! Helper to send a mouse button event through the broadcast channel
  void SendMouseButtonEvent(oxygen::platform::MouseButton button,
    oxygen::platform::ButtonState state,
    oxygen::SubPixelPosition position = { .x = 0.0F, .y = 0.0F });

  //! Helper to send a mouse motion event through the broadcast channel
  void SendMouseMotion(float dx, float dy,
    oxygen::SubPixelPosition position = { .x = 0.0F, .y = 0.0F });

  //! Helper to send a mouse wheel event through the broadcast channel
  void SendMouseWheel(float dx, float dy,
    oxygen::SubPixelPosition position = { .x = 0.0F, .y = 0.0F });

  [[nodiscard]] auto Now() const noexcept { return phy_time_.Now(); }

  //! Event loop driving the coroutine-based InputSystem pipeline.
  [[nodiscard]] auto Loop() noexcept -> oxygen::co::testing::TestEventLoop&
  {
    return loop_;
  }

  //! The InputSystem under test.
  [[nodiscard]] auto System() const noexcept -> oxygen::engine::InputSystem&
  {
    return *input_system_;
  }

  //! The frame context passed to the InputSystem phase handlers.
  [[nodiscard]] auto Context() const noexcept
    -> oxygen::observer_ptr<oxygen::engine::FrameContext>
  {
    return oxygen::observer_ptr { frame_context_.get() };
  }

private:
  oxygen::co::testing::TestEventLoop loop_;
  std::unique_ptr<oxygen::co::BroadcastChannel<oxygen::platform::InputEvent>>
    input_channel_;
  std::unique_ptr<oxygen::engine::InputSystem> input_system_;
  std::unique_ptr<oxygen::engine::FrameContext> frame_context_;

  oxygen::time::PhysicalClock phy_time_;
};

} // namespace oxygen::input::testing
