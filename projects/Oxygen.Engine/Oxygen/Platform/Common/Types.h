//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include "Oxygen/Base/Types.h"

namespace oxygen {

class Platform;

using PlatformPtr = std::shared_ptr<Platform>;

namespace platform {

  class Display;
  class Window;
  class InputEvent;
  class KeyEvent;
  class MouseButtonEvent;
  class MouseWheelEvent;
  class MouseMotionEvent;
  class InputSlot;
  class InputSlots;

  using WindowPtr = std::weak_ptr<Window>;

  using WindowIdType = uint32_t;
  [[maybe_unused]] constexpr WindowIdType kInvalidWindowId = 0;

  inline auto to_string(WindowIdType window_id)
  {
    return nostd::to_string(window_id);
  }

  enum class Key : uint8_t {
    kNone = 0, // Indicates an unsupported key code that should never be
    // transmitted in an event.

    kBackSpace, // The backspace key.
    kDelete, // The forward delete key.
    kTab, // The tab key.
    kClear, // The Clear key.
    kReturn, // Return key.
    kPause, // Pause on PC machines.
    kEscape, // Escape key.
    kSpace, // Space key.

    kKeypad0, // Numeric keypad 0.
    kKeypad1, // Numeric keypad 1.
    kKeypad2, // Numeric keypad 2.
    kKeypad3, // Numeric keypad 3.
    kKeypad4, // Numeric keypad 4.
    kKeypad5, // Numeric keypad 5.
    kKeypad6, // Numeric keypad 6.
    kKeypad7, // Numeric keypad 7.
    kKeypad8, // Numeric keypad 8.
    kKeypad9, // Numeric keypad 9.
    kKeypadPeriod, // Numeric keypad '.'.
    kKeypadDivide, // Numeric keypad '/'.
    kKeypadMultiply, // Numeric keypad '*'.
    kKeypadMinus, // Numeric keypad '-'.
    kKeypadPlus, // Numeric keypad '+'.
    kKeypadEnter, // Numeric keypad Enter.
    kKeypadEquals, // Numeric keypad '='.

    kUpArrow, // Up arrow key.
    kDownArrow, // Down arrow key.
    kRightArrow, // Right arrow key.
    kLeftArrow, // Left arrow key.

    kInsert, // Insert key.
    kHome, // Home key.
    kEnd, // End key.
    kPageUp, // Page up.
    kPageDown, // Page down.

    kF1, // F1 function key.
    kF2, // F2 function key.
    kF3, // F3 function key.
    kF4, // F4 function key.
    kF5, // F5 function key.
    kF6, // F6 function key.
    kF7, // F7 function key.
    kF8, // F8 function key.
    kF9, // F9 function key.
    kF10, // F10 function key.
    kF11, // F11 function key.
    kF12, // F12 function key.
    kF13, // F13 function key.
    kF14, // F14 function key.
    kF15, // F15 function key.

    kAlpha0, // The '0' key on the top of the alphanumeric keyboard.
    kAlpha1, // The '1' key on the top of the alphanumeric keyboard.
    kAlpha2, // The '2' key on the top of the alphanumeric keyboard.
    kAlpha3, // The '3' key on the top of the alphanumeric keyboard.
    kAlpha4, // The '4' key on the top of the alphanumeric keyboard.
    kAlpha5, // The '5' key on the top of the alphanumeric keyboard.
    kAlpha6, // The '6' key on the top of the alphanumeric keyboard.
    kAlpha7, // The '7' key on the top of the alphanumeric keyboard.
    kAlpha8, // The '8' key on the top of the alphanumeric keyboard.
    kAlpha9, // The '9' key on the top of the alphanumeric keyboard.
    kExclaim, // Exclamation mark key '!'.
    kDoubleQuote, // Double quote key '"'.
    kHash, // Hash key '#'.
    kDollar, // Dollar sign key '$'.
    kPercent, // Percent '%' key.
    kAmpersand, // Ampersand key '&'.
    kQuote, // Quote key '.
    kLeftParen, // Left Parenthesis key '('.
    kRightParen, // Right Parenthesis key ')'.
    kAsterisk, // Asterisk key '*'.
    kPlus, // Plus key '+'.
    kComma, // Comma ',' key.
    kMinus, // Minus '-' key.
    kPeriod, // Period '.' key.
    kSlash, // Slash '/' key.
    kColon, // Colon ':' key.
    kSemicolon, // Semicolon ';' key.
    kLess, // Less than '<' key.
    kEquals, // Equals '=' key.
    kGreater, // Greater than '>' key.
    kQuestion, // Question mark '?' key.
    kAt, // At key '@'.
    kLeftBracket, // Left square bracket key '['.
    kBackslash, // Backslash key '\'.
    kRightBracket, // Right square bracket key ']'.
    kCaret, // Caret key '^'.
    kUnderscore, // Underscore '_' key.
    kBackQuote, // Back quote key '`'.
    kA, // 'a' key.
    kB, // 'b' key.
    kC, // 'c' key.
    kD, // 'd' key.
    kE, // 'e' key.
    kF, // 'f' key.
    kG, // 'g' key.
    kH, // 'h' key.
    kI, // 'i' key.
    kJ, // 'j' key.
    kK, // 'k' key.
    kL, // 'l' key.
    kM, // 'm' key.
    kN, // 'n' key.
    kO, // 'o' key.
    kP, // 'p' key.
    kQ, // 'q' key.
    kR, // 'r' key.
    kS, // 's' key.
    kT, // 't' key.
    kU, // 'u' key.
    kV, // 'v' key.
    kW, // 'w' key.
    kX, // 'x' key.
    kY, // 'y' key.
    kZ, // 'z' key.
    kLeftCurlyBracket, // Left curly bracket key '{'.
    kPipe, // Pipe '|' key.
    kRightCurlyBracket, // Right curly bracket key '}'.
    kTilde, // Tilde '~' key.

    kNumLock, // Num lock key.
    kCapsLock, // Caps lock key.
    kScrollLock, // Scroll lock key.
    kRightShift, // Right shift key.
    kLeftShift, // Left shift key.
    kRightControl, // Right Control key.
    kLeftControl, // Left Control key.
    kRightAlt, // Right Alt key.
    kLeftAlt, // Left Alt key.
    kRightMeta, // Right Windows key or right Command key
    kLeftMeta, // Left Windows key or left Command key

    kHelp, // Help key.
    kPrint, // Print key.
    kSysReq, // Sys Req key.
    kMenu, // Menu key.
  };

  enum class ButtonState : uint8_t {
    kReleased, // Key has just been released this frame.
    kPressed, // Key has just been pressed down this frame.
  };

  enum class MouseButton : uint8_t {
    kNone = 0, // Indicates an unsupported mouse button that should never be
    // transmitted in an event.

    kLeft = 1 << 0,
    kRight = 1 << 1,
    kMiddle = 1 << 2,
    kExtButton1 = 1 << 3,
    kExtButton2 = 1 << 4,
  };

} // namespace platform

} // namespace oxygen
