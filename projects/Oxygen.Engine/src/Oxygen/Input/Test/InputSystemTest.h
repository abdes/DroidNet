//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/input.h>

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
    oxygen::SubPixelPosition position = { 0.0f, 0.0f });

  //! Helper to send a mouse motion event through the broadcast channel
  void SendMouseMotion(
    float dx, float dy, oxygen::SubPixelPosition position = { 0.0f, 0.0f });

  //! Helper to send a mouse wheel event through the broadcast channel
  void SendMouseWheel(
    float dx, float dy, oxygen::SubPixelPosition position = { 0.0f, 0.0f });

  [[nodiscard]] auto Now() const noexcept { return phy_time_.Now(); }

protected:
  oxygen::co::testing::TestEventLoop loop_;
  std::unique_ptr<oxygen::co::BroadcastChannel<oxygen::platform::InputEvent>>
    input_channel_;
  std::unique_ptr<oxygen::engine::InputSystem> input_system_;
  std::unique_ptr<oxygen::engine::FrameContext> frame_context_;

  oxygen::time::PhysicalClock phy_time_;
};

} // namespace oxygen::input::testing
