//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <SDL3/SDL.h>

#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

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

namespace {
auto MapKeyCode(const SDL_Keycode code)
{
    using oxygen::platform::Key;
    // clang-format off
    switch (code) {
    case SDLK_BACKSPACE: return Key::kBackSpace;
    case SDLK_DELETE: return Key::kDelete;
    case SDLK_TAB: return Key::kTab;
    case SDLK_CLEAR: return Key::kClear;
    case SDLK_RETURN: return Key::kReturn;
    case SDLK_PAUSE: return Key::kPause;
    case SDLK_ESCAPE: return Key::kEscape;
    case SDLK_SPACE: return Key::kSpace;
    case SDLK_KP_0: return Key::kKeypad0;
    case SDLK_KP_1: return Key::kKeypad1;
    case SDLK_KP_2: return Key::kKeypad2;
    case SDLK_KP_3: return Key::kKeypad3;
    case SDLK_KP_4: return Key::kKeypad4;
    case SDLK_KP_5: return Key::kKeypad5;
    case SDLK_KP_6: return Key::kKeypad6;
    case SDLK_KP_7: return Key::kKeypad7;
    case SDLK_KP_8: return Key::kKeypad8;
    case SDLK_KP_9: return Key::kKeypad9;
    case SDLK_KP_PERIOD: return Key::kKeypadPeriod;
    case SDLK_KP_DIVIDE: return Key::kKeypadDivide;
    case SDLK_KP_MULTIPLY: return Key::kKeypadMultiply;
    case SDLK_KP_MINUS: return Key::kKeypadMinus;
    case SDLK_KP_PLUS: return Key::kKeypadPlus;
    case SDLK_KP_ENTER: return Key::kKeypadEnter;
    case SDLK_KP_EQUALS: return Key::kKeypadEquals;
    case SDLK_UP: return Key::kUpArrow;
    case SDLK_DOWN: return Key::kDownArrow;
    case SDLK_RIGHT: return Key::kRightArrow;
    case SDLK_LEFT: return Key::kLeftArrow;
    case SDLK_INSERT: return Key::kInsert;
    case SDLK_HOME: return Key::kHome;
    case SDLK_END: return Key::kEnd;
    case SDLK_PAGEUP: return Key::kPageUp;
    case SDLK_PAGEDOWN: return Key::kPageDown;
    case SDLK_F1: return Key::kF1;
    case SDLK_F2: return Key::kF2;
    case SDLK_F3: return Key::kF3;
    case SDLK_F4: return Key::kF4;
    case SDLK_F5: return Key::kF5;
    case SDLK_F6: return Key::kF6;
    case SDLK_F7: return Key::kF7;
    case SDLK_F8: return Key::kF8;
    case SDLK_F9: return Key::kF9;
    case SDLK_F10: return Key::kF10;
    case SDLK_F11: return Key::kF11;
    case SDLK_F12: return Key::kF12;
    case SDLK_F13: return Key::kF13;
    case SDLK_F14: return Key::kF14;
    case SDLK_F15: return Key::kF15;
    case SDLK_0: return Key::kAlpha0;
    case SDLK_1: return Key::kAlpha1;
    case SDLK_2: return Key::kAlpha2;
    case SDLK_3: return Key::kAlpha3;
    case SDLK_4: return Key::kAlpha4;
    case SDLK_5: return Key::kAlpha5;
    case SDLK_6: return Key::kAlpha6;
    case SDLK_7: return Key::kAlpha7;
    case SDLK_8: return Key::kAlpha8;
    case SDLK_9: return Key::kAlpha9;
    case SDLK_EXCLAIM: return Key::kExclaim;
    case SDLK_DBLAPOSTROPHE: return Key::kDoubleQuote;
    case SDLK_HASH: return Key::kHash;
    case SDLK_DOLLAR: return Key::kDollar;
    case SDLK_PERCENT: return Key::kPercent;
    case SDLK_AMPERSAND: return Key::kAmpersand;
    case SDLK_APOSTROPHE: return Key::kQuote;
    case SDLK_LEFTPAREN: return Key::kLeftParen;
    case SDLK_RIGHTPAREN: return Key::kRightParen;
    case SDLK_ASTERISK: return Key::kAsterisk;
    case SDLK_PLUS: return Key::kPlus;
    case SDLK_COMMA: return Key::kComma;
    case SDLK_MINUS: return Key::kMinus;
    case SDLK_PERIOD: return Key::kPeriod;
    case SDLK_SLASH: return Key::kSlash;
    case SDLK_COLON: return Key::kColon;
    case SDLK_SEMICOLON: return Key::kSemicolon;
    case SDLK_LESS: return Key::kLess;
    case SDLK_EQUALS: return Key::kEquals;
    case SDLK_GREATER: return Key::kGreater;
    case SDLK_QUESTION: return Key::kQuestion;
    case SDLK_AT: return Key::kAt;
    case SDLK_LEFTBRACKET: return Key::kLeftBracket;
    case SDLK_BACKSLASH: return Key::kBackslash;
    case SDLK_RIGHTBRACKET: return Key::kRightBracket;
    case SDLK_CARET: return Key::kCaret;
    case SDLK_UNDERSCORE: return Key::kUnderscore;
    case SDLK_GRAVE: return Key::kBackQuote;
    case SDLK_A: return Key::kA;
    case SDLK_B: return Key::kB;
    case SDLK_C: return Key::kC;
    case SDLK_D: return Key::kD;
    case SDLK_E: return Key::kE;
    case SDLK_F: return Key::kF;
    case SDLK_G: return Key::kG;
    case SDLK_H: return Key::kH;
    case SDLK_I: return Key::kI;
    case SDLK_J: return Key::kJ;
    case SDLK_K: return Key::kK;
    case SDLK_L: return Key::kL;
    case SDLK_M: return Key::kM;
    case SDLK_N: return Key::kN;
    case SDLK_O: return Key::kO;
    case SDLK_P: return Key::kP;
    case SDLK_Q: return Key::kQ;
    case SDLK_R: return Key::kR;
    case SDLK_S: return Key::kS;
    case SDLK_T: return Key::kT;
    case SDLK_U: return Key::kU;
    case SDLK_V: return Key::kV;
    case SDLK_W: return Key::kW;
    case SDLK_X: return Key::kX;
    case SDLK_Y: return Key::kY;
    case SDLK_Z: return Key::kZ;
    case SDLK_NUMLOCKCLEAR: return Key::kNumLock;
    case SDLK_CAPSLOCK: return Key::kCapsLock;
    case SDLK_SCROLLLOCK: return Key::kScrollLock;
    case SDLK_RSHIFT: return Key::kRightShift;
    case SDLK_LSHIFT: return Key::kLeftShift;
    case SDLK_RCTRL: return Key::kRightControl;
    case SDLK_LCTRL: return Key::kLeftControl;
    case SDLK_RALT: return Key::kRightAlt;
    case SDLK_LALT: return Key::kLeftAlt;
    case SDLK_LGUI: return Key::kLeftMeta;
    case SDLK_RGUI: return Key::kRightMeta;
    case SDLK_HELP: return Key::kHelp;
    case SDLK_PRINTSCREEN: return Key::kPrint;
    case SDLK_SYSREQ: return Key::kSysReq;
    case SDLK_MENU: return Key::kMenu;
    default: return Key::kNone;
    }
    // clang-format on
}

auto TranslateKeyboardEvent(SDL_Event const& event)
    -> std::unique_ptr<InputEvent>
{
    LOG_SCOPE_F(0, "Keyboard event");
    DLOG_F(0, "type      = {}",
        ((event.key.type == SDL_EVENT_KEY_UP) ? "KEY_UP" : "KEY_DOWN"));
    DLOG_F(0, "window id = {}", event.key.windowID);
    DLOG_F(0, "repeat    = {}", event.key.repeat);
    DLOG_F(0, "scancode  = {}", static_cast<uint32_t>(event.key.scancode));
    DLOG_F(0, "keycode   = {}", event.key.key);
    DLOG_F(0, "key name  = {}", GetKeyName(event.key.key));

    const auto key_code = MapKeyCode(event.key.key);
    if (key_code == Key::kNone) {
        // This is not a key code we are interested to handle.
        // Do not generate an event for it
        const uint32_t key = event.key.key;
        const uint32_t scan_code = event.key.scancode;
        DLOG_F(0,
            "Keyboard event with key code = {} (scan code = {}) is not "
            "something we can handle. Ignoring event.",
            key,
            scan_code);
        return {};
    }

    const KeyInfo key_info(key_code, event.key.repeat);
    const ButtonState button_state = event.key.down ? ButtonState::kPressed : ButtonState::kReleased;

    return std::make_unique<KeyEvent>(
        std::chrono::duration_cast<oxygen::TimePoint>(
            std::chrono::nanoseconds(event.key.timestamp)),
        event.key.windowID,
        key_info,
        button_state);
}

auto MapMouseButton(auto button)
{
    using oxygen::platform::MouseButton;
    // clang-format off
    switch (button) {
    case SDL_BUTTON_LEFT: return MouseButton::kLeft;
    case SDL_BUTTON_RIGHT: return MouseButton::kRight;
    case SDL_BUTTON_MIDDLE: return MouseButton::kMiddle;
    case SDL_BUTTON_X1: return MouseButton::kExtButton1;
    case SDL_BUTTON_X2: return MouseButton::kExtButton2;
    default: return MouseButton::kNone;
    }
    // clang-format on
}

auto TranslateMouseButtonEvent(const SDL_Event& event)
    -> std::unique_ptr<InputEvent>
{
    LOG_SCOPE_F(0, "Mouse button event");
    DLOG_F(0, "button = {}", event.button.button);
    DLOG_F(
        2,
        "state  = {}",
        ((event.button.type == SDL_EVENT_MOUSE_BUTTON_UP) ? "UP" : "DOWN"));

    const auto button = MapMouseButton(event.button.button);
    if (button == MouseButton::kNone) {
        // This is not a mouse button we are interested to handle.
        // Do not generate an event for it
        DLOG_F(
            2,
            "Mouse button event with button = {} is not something we can handle. "
            "Ignoring event.",
            event.button.button);
        return {};
    }

    const ButtonState button_state = event.button.down ? ButtonState::kPressed : ButtonState::kReleased;

    return std::make_unique<MouseButtonEvent>(
        std::chrono::duration_cast<oxygen::TimePoint>(
            std::chrono::nanoseconds(event.button.timestamp)),
        event.key.windowID,
        SubPixelPosition {
            .x = event.button.x,
            .y = event.button.y,
        },
        button,
        button_state);
}

auto TranslateMouseMotionEvent(const SDL_Event& event)
    -> std::unique_ptr<InputEvent>
{
    LOG_SCOPE_F(0, "Mouse motion event");
    DLOG_F(0, "dx = {}", event.motion.xrel);
    DLOG_F(0, "dy = {}", event.motion.yrel);

    auto motion_event = std::make_unique<MouseMotionEvent>(
        std::chrono::duration_cast<oxygen::TimePoint>(
            std::chrono::nanoseconds(event.motion.timestamp)),
        event.key.windowID,
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
    LOG_SCOPE_F(0, "Mouse wheel event");
    DLOG_F(0, "dx = {}", event.wheel.x);
    DLOG_F(0, "dy = {}", event.wheel.y);

    const auto direction = event.wheel.direction == SDL_MOUSEWHEEL_NORMAL ? 1.0F : -1.0F;

    return std::make_unique<MouseWheelEvent>(
        std::chrono::duration_cast<oxygen::TimePoint>(
            std::chrono::nanoseconds(event.wheel.timestamp)),
        event.key.windowID,
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

auto InputEvents::ProcessPlatformEvents() const -> co::Co<>
{
    while (true) {
        auto& event = co_await event_pump_->NextEvent();
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
                const auto success = channel_writer_.TrySend(std::move(*input_event));
                DCHECK_F(success);
            } else {
                // We can unlock the event pump here as we are already full
                // and we will most likely not be able to process the next
                // event anyway. This will allow other components to process
                // their events.
                lock.Release();
                co_await channel_writer_.Send(std::move(*input_event));
            }
            event.SetHandled();
        }
    }
}
