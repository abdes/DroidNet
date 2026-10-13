//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/SDL/InputTranslation.h>

namespace oxygen::platform::sdl {

auto MapKeyCode(const SDL_Keycode code) -> Key
{
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

auto MapMouseButton(const Uint8 button) -> MouseButton
{
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

} // namespace oxygen::platform::sdl
