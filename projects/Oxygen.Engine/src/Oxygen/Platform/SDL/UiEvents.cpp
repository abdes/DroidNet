//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>

#include <SDL3/SDL_events.h>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/SDL/InputTranslation.h>
#include <Oxygen/Platform/SDL/UiEvents.h>
#include <Oxygen/Platform/UiEvent.h>

namespace oxygen::platform::sdl {

namespace {

  auto TranslateKey(const SDL_KeyboardEvent& event) -> std::optional<UiEvent>
  {
    const auto key = MapKeyCode(event.key);
    if (key == Key::kNone) {
      return std::nullopt;
    }
    return UiKeyEvent {
      .window_id = event.windowID,
      .key = key,
      .pressed = event.down,
      .repeat = event.repeat,
    };
  }

  auto TranslateMouseButton(const SDL_MouseButtonEvent& event)
    -> std::optional<UiEvent>
  {
    const auto button = MapMouseButton(event.button);
    if (button == MouseButton::kNone) {
      return std::nullopt;
    }
    return UiMouseButtonEvent {
      .window_id = event.windowID,
      .button = button,
      .pressed = event.down,
      .position = SubPixelPosition { .x = event.x, .y = event.y },
    };
  }

} // namespace

auto TranslateUiEvent(const PlatformEvent& event) -> std::optional<UiEvent>
{
  const auto* const native = event.NativeEventAs<SDL_Event>();
  if (native == nullptr) {
    return std::nullopt;
  }

  switch (native->type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    return TranslateKey(native->key);
  case SDL_EVENT_MOUSE_MOTION:
    return UiMouseMotionEvent {
      .window_id = native->motion.windowID,
      .position
      = SubPixelPosition { .x = native->motion.x, .y = native->motion.y },
    };
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    return TranslateMouseButton(native->button);
  case SDL_EVENT_MOUSE_WHEEL:
    // The values already reflect the user's scroll direction preference.
    return UiMouseWheelEvent {
      .window_id = native->wheel.windowID,
      .scroll = SubPixelMotion { .dx = native->wheel.x, .dy = native->wheel.y },
    };
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    return UiMouseLeaveEvent { .window_id = native->window.windowID };
  case SDL_EVENT_TEXT_INPUT:
    return UiTextInputEvent {
      .window_id = native->text.windowID,
      .text = native->text.text != nullptr ? native->text.text : "",
    };
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    return UiFocusEvent {
      .window_id = native->window.windowID,
      .gained = native->type == SDL_EVENT_WINDOW_FOCUS_GAINED,
    };
  default:
    return std::nullopt;
  }
}

} // namespace oxygen::platform::sdl
