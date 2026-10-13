//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/SDL/InputTranslation.h>
#include <Oxygen/Platform/SDL/Wrapper.h>

using oxygen::SubPixelMotion;
using oxygen::SubPixelPosition;
using oxygen::platform::ButtonState;
using oxygen::platform::InputEvent;
using oxygen::platform::InputEvents;
using oxygen::platform::Key;
using oxygen::platform::KeyEvent;
using oxygen::platform::MouseButton;
using oxygen::platform::MouseButtonEvent;
using oxygen::platform::MouseMotionEvent;
using oxygen::platform::MouseWheelEvent;
using oxygen::platform::input::KeyInfo;
using oxygen::platform::sdl::GetKeyName;
using oxygen::platform::sdl::MapKeyCode;
using oxygen::platform::sdl::MapMouseButton;
using oxygen::time::PhysicalTime;

namespace {
auto TranslateKeyboardEvent(const SDL_Event& event)
  -> std::unique_ptr<InputEvent>
{
  LOG_SCOPE_F(2, "Keyboard event");
  DLOG_F(2, "type      = {}",
    ((event.key.type == SDL_EVENT_KEY_UP) ? "KEY_UP" : "KEY_DOWN"));
  DLOG_F(2, "window id = {}", event.key.windowID);
  DLOG_F(2, "repeat    = {}", event.key.repeat);
  DLOG_F(2, "scancode  = {}", static_cast<uint32_t>(event.key.scancode));
  DLOG_F(2, "keycode   = {}", event.key.key);
  DLOG_F(2, "key name  = {}", GetKeyName(event.key.key));

  const auto key_code = MapKeyCode(event.key.key);
  if (key_code == Key::kNone) {
    // This is not a key code we are interested to handle. Do not generate an
    // event for it
    DLOG_F(2,
      "Keyboard event with key code = {} (scan code = {}) is not "
      "something we can handle. Ignoring event.",
      event.key.key, event.key.scancode);
    return {};
  }

  const KeyInfo key_info(key_code, event.key.repeat);
  const ButtonState button_state
    = event.key.down ? ButtonState::kPressed : ButtonState::kReleased;

  // Convert the timestamp to a PhysicalTime time point
  const auto ts = PhysicalTime { PhysicalTime::UnderlyingType {
    std::chrono::nanoseconds(event.key.timestamp) } };
  return std::make_unique<KeyEvent>(
    ts, event.key.windowID, key_info, button_state);
}

auto TranslateMouseButtonEvent(const SDL_Event& event)
  -> std::unique_ptr<InputEvent>
{
  LOG_SCOPE_F(2, "Mouse button event");
  DLOG_F(2, "button = {}", event.button.button);
  DLOG_F(2, "state  = {}",
    ((event.button.type == SDL_EVENT_MOUSE_BUTTON_UP) ? "UP" : "DOWN"));

  const auto button = MapMouseButton(event.button.button);
  if (button == MouseButton::kNone) {
    // This is not a mouse button we are interested to handle. Do not generate
    // an event for it
    DLOG_F(2,
      "Mouse button event with button = {} is not something we can handle. "
      "Ignoring event.",
      event.button.button);
    return {};
  }

  const ButtonState button_state
    = event.button.down ? ButtonState::kPressed : ButtonState::kReleased;

  // Convert the timestamp to a PhysicalTime time point
  const auto ts = PhysicalTime { PhysicalTime::UnderlyingType {
    std::chrono::nanoseconds(event.button.timestamp) } };
  return std::make_unique<MouseButtonEvent>(ts, event.button.windowID,
    SubPixelPosition {
      .x = event.button.x,
      .y = event.button.y,
    },
    button, button_state);
}

auto TranslateMouseMotionEvent(const SDL_Event& event)
  -> std::unique_ptr<InputEvent>
{
  LOG_SCOPE_F(2, "Mouse motion event");
  DLOG_F(2, "dx = {}", event.motion.xrel);
  DLOG_F(2, "dy = {}", event.motion.yrel);

  const auto ts = PhysicalTime { PhysicalTime::UnderlyingType {
    std::chrono::nanoseconds(event.motion.timestamp) } };
  auto motion_event
    = std::make_unique<MouseMotionEvent>(ts, event.motion.windowID,
      SubPixelPosition {
        .x = event.motion.x,
        .y = event.motion.y,
      },
      SubPixelMotion {
        .dx = event.motion.xrel,
        .dy = event.motion.yrel,
      });
  return motion_event;
}

auto TranslateMouseWheelEvent(const SDL_Event& event)
  -> std::unique_ptr<InputEvent>
{
  LOG_SCOPE_F(2, "Mouse wheel event");
  DLOG_F(2, "dx = {}", event.wheel.x);
  DLOG_F(2, "dy = {}", event.wheel.y);

  const auto direction
    = event.wheel.direction == SDL_MOUSEWHEEL_NORMAL ? 1.0F : -1.0F;

  const auto ts = PhysicalTime { PhysicalTime::UnderlyingType {
    std::chrono::nanoseconds(event.wheel.timestamp) } };
  return std::make_unique<MouseWheelEvent>(ts, event.wheel.windowID,
    SubPixelPosition {
      .x = event.wheel.mouse_x,
      .y = event.wheel.mouse_y,
    },
    SubPixelMotion {
      .dx = direction * event.wheel.x,
      .dy = direction * event.wheel.y,
    });
}
} // namespace

auto InputEvents::ProcessPlatformEvents() -> co::Co<>
{
  // No event pump means we are in headless mode, and we do not pull events
  // from anywhere. We just accept injected events via the writer.
  if (!event_pump_) {
    co_return;
  }

  while (async_->IsRunning()) {
    // Check if the event pump is still running. If not, the next event is a
    // dummy one that we should just ignore, and this loop should immediately
    // terminate.
    if (!event_pump_->IsRunning()) {
      event_pump_ = nullptr;
      break;
    }

    const auto& event = co_await event_pump_->NextEvent();

    // Check if we were woken up due to shut down
    if (!event_pump_->IsRunning()) {
      break;
    }

    auto lock = co_await event_pump_->Lock();
    if (event.IsHandled()) {
      continue;
    }

    std::unique_ptr<InputEvent> input_event;
    if (auto& sdl_event = *event.NativeEventAs<SDL_Event>();
      sdl_event.type == SDL_EVENT_MOUSE_MOTION) {
      input_event = TranslateMouseMotionEvent(sdl_event);
    } else if (sdl_event.type == SDL_EVENT_KEY_UP
      || sdl_event.type == SDL_EVENT_KEY_DOWN) {
      input_event = TranslateKeyboardEvent(sdl_event);
    } else if (sdl_event.type == SDL_EVENT_MOUSE_BUTTON_UP
      || sdl_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      input_event = TranslateMouseButtonEvent(sdl_event);
    } else if (sdl_event.type == SDL_EVENT_MOUSE_WHEEL) {
      input_event = TranslateMouseWheelEvent(sdl_event);
    }
    if (input_event) {
      // Try to send synchronously if possible
      if (!channel_.Full()) {
        [[maybe_unused]] const auto success
          = channel_writer_.TrySend(std::move(input_event));
        DCHECK_F(success);
      } else {
        // We can unlock the event pump here as we are already full , and we
        // will most likely not be able to process the next event anyway. This
        // will allow other components to process their events.
        lock.Release();
        co_await channel_writer_.Send(std::move(input_event));
      }
      event.SetHandled();
    }
  }
  DLOG_F(INFO, "DONE: platform Input Events async processing");
}
