//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Input/Test/InputSystemTest.h>
#include <Oxygen/Platform/Types.h>

// Implementation of EngineTagFactory. Provides access to EngineTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal
#endif

namespace oxygen::input::testing {

void InputSystemTest::SetUp()
{
  // CRITICAL: Initialize platform input slots mapping
  oxygen::platform::InputSlots::Initialize();

  // Arrange - Create test broadcast channel for input events
  input_channel_ = std::make_unique<
    oxygen::co::BroadcastChannel<oxygen::platform::InputEvent>>(32);
  input_system_
    = std::make_unique<oxygen::engine::InputSystem>(input_channel_->ForRead());

  // Create mock FrameContext for testing
  frame_context_ = std::make_unique<oxygen::engine::FrameContext>();
}

void InputSystemTest::TearDown()
{
  // Clean up
  input_system_.reset();
  input_channel_.reset();
  frame_context_.reset();
}

void InputSystemTest::SendKeyEvent(
  oxygen::platform::Key key, oxygen::platform::ButtonState state)
{
  using oxygen::platform::KeyEvent;
  using oxygen::platform::kInvalidWindowId;
  using oxygen::platform::input::KeyInfo;

  auto key_info = KeyInfo(key, false); // Not a repeat

  auto event
    = std::make_shared<KeyEvent>(Now(), kInvalidWindowId, key_info, state);

  auto& writer = input_channel_->ForWrite();
  bool sent = writer.TrySend(event);
  ASSERT_TRUE(sent) << "Failed to send key event to broadcast channel";
}

void InputSystemTest::SendMouseButtonEvent(oxygen::platform::MouseButton button,
  oxygen::platform::ButtonState state, oxygen::SubPixelPosition position)
{
  using oxygen::platform::kInvalidWindowId;
  using oxygen::platform::MouseButtonEvent;

  auto event = std::make_shared<MouseButtonEvent>(
    Now(), kInvalidWindowId, position, button, state);

  auto& writer = input_channel_->ForWrite();
  bool sent = writer.TrySend(event);
  ASSERT_TRUE(sent) << "Failed to send mouse button event to broadcast channel";
}

void InputSystemTest::SendMouseMotion(
  float dx, float dy, oxygen::SubPixelPosition position)
{
  using oxygen::platform::kInvalidWindowId;
  using oxygen::platform::MouseMotionEvent;

  auto event = std::make_shared<MouseMotionEvent>(
    Now(), kInvalidWindowId, position, SubPixelMotion { dx, dy });
  auto& writer = input_channel_->ForWrite();
  bool sent = writer.TrySend(event);
  ASSERT_TRUE(sent) << "Failed to send mouse motion event to broadcast channel";
}

void InputSystemTest::SendMouseWheel(
  float dx, float dy, oxygen::SubPixelPosition position)
{
  using oxygen::platform::kInvalidWindowId;
  using oxygen::platform::MouseWheelEvent;

  auto event = std::make_shared<MouseWheelEvent>(
    Now(), kInvalidWindowId, position, SubPixelMotion { dx, dy });
  auto& writer = input_channel_->ForWrite();
  bool sent = writer.TrySend(event);
  ASSERT_TRUE(sent) << "Failed to send mouse wheel event to broadcast channel";
}

} // namespace oxygen::input::testing
