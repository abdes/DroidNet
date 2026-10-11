//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Platform/InputApiExport.h>

namespace oxygen::platform {

//------------------------------------------------------------------------------
// Base enums for keys, mouse buttons
//------------------------------------------------------------------------------

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

enum class MouseButton : uint8_t {
  kNone = 0, // Unsupported mouse button that should never be transmitted in an
             // event.

  kLeft = 1 << 0,
  kRight = 1 << 1,
  kMiddle = 1 << 2,
  kExtButton1 = 1 << 3,
  kExtButton2 = 1 << 4,
};

//------------------------------------------------------------------------------
// InputSlot
//------------------------------------------------------------------------------

namespace detail {
  class InputSlotDetails;
} // namespace detail

class InputSlot {
public:
  explicit InputSlot(const std::string_view name)
    : name_(name)
  {
  }

  [[nodiscard]] auto GetName() const { return name_; }

  OXGN_PLIN_API auto IsModifierKey() const -> bool;
  OXGN_PLIN_API auto IsKeyboardKey() const -> bool;
  OXGN_PLIN_API auto IsMouseButton() const -> bool;

  OXGN_PLIN_API auto IsAxis1D() const -> bool;
  OXGN_PLIN_API auto IsAxis2D() const -> bool;
  OXGN_PLIN_API auto IsAxis3D() const -> bool;

  friend class InputSlots;

  OXGN_PLIN_NDAPI auto GetDisplayString() const -> std::string_view;

  OXGN_PLIN_NDAPI auto GetInputCategoryName() const -> std::string_view;

  friend auto operator==(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ == rhs.name_;
  }
  friend auto operator!=(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ != rhs.name_;
  }
  friend auto operator<(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ < rhs.name_;
  }

private:
  std::string_view name_;
  mutable std::shared_ptr<detail::InputSlotDetails> details_;

  auto UpdateDetailsIfNotUpdated() const -> void;
};
} // namespace oxygen::platform

template <> struct std::hash<oxygen::platform::InputSlot> {
  auto operator()(const oxygen::platform::InputSlot& slot) const noexcept
    -> size_t
  {
    return hash<std::string_view>()(slot.GetName());
  }
};

//------------------------------------------------------------------------------
// InputSlots
//------------------------------------------------------------------------------

namespace oxygen::platform {

class InputSlots {
public:
  // Category names static string_view literals
  OXGN_PLIN_API static const std::string_view kKeyCategoryName;
  OXGN_PLIN_API static const std::string_view kMouseCategoryName;

  // -- Static input slots
  // NOLINTBEGIN
  // Mouse slots
  OXGN_PLIN_API static const InputSlot MouseWheelUp;
  OXGN_PLIN_API static const InputSlot MouseWheelDown;
  OXGN_PLIN_API static const InputSlot MouseWheelLeft;
  OXGN_PLIN_API static const InputSlot MouseWheelRight;
  OXGN_PLIN_API static const InputSlot MouseWheelX;
  OXGN_PLIN_API static const InputSlot MouseWheelY;
  OXGN_PLIN_API static const InputSlot MouseWheelXY;
  OXGN_PLIN_API static const InputSlot LeftMouseButton;
  OXGN_PLIN_API static const InputSlot RightMouseButton;
  OXGN_PLIN_API static const InputSlot MiddleMouseButton;
  OXGN_PLIN_API static const InputSlot ThumbMouseButton1;
  OXGN_PLIN_API static const InputSlot ThumbMouseButton2;
  OXGN_PLIN_API static const InputSlot MouseX;
  OXGN_PLIN_API static const InputSlot MouseY;
  OXGN_PLIN_API static const InputSlot MouseXY;

  // Keyboard slots
  OXGN_PLIN_API static const InputSlot None;
  OXGN_PLIN_API static const InputSlot AnyKey;
  OXGN_PLIN_API static const InputSlot BackSpace;
  OXGN_PLIN_API static const InputSlot Delete;
  OXGN_PLIN_API static const InputSlot Tab;
  OXGN_PLIN_API static const InputSlot Clear;
  OXGN_PLIN_API static const InputSlot Return;
  OXGN_PLIN_API static const InputSlot Pause;
  OXGN_PLIN_API static const InputSlot Escape;
  OXGN_PLIN_API static const InputSlot Space;
  OXGN_PLIN_API static const InputSlot Keypad0;
  OXGN_PLIN_API static const InputSlot Keypad1;
  OXGN_PLIN_API static const InputSlot Keypad2;
  OXGN_PLIN_API static const InputSlot Keypad3;
  OXGN_PLIN_API static const InputSlot Keypad4;
  OXGN_PLIN_API static const InputSlot Keypad5;
  OXGN_PLIN_API static const InputSlot Keypad6;
  OXGN_PLIN_API static const InputSlot Keypad7;
  OXGN_PLIN_API static const InputSlot Keypad8;
  OXGN_PLIN_API static const InputSlot Keypad9;
  OXGN_PLIN_API static const InputSlot KeypadPeriod;
  OXGN_PLIN_API static const InputSlot KeypadDivide;
  OXGN_PLIN_API static const InputSlot KeypadMultiply;
  OXGN_PLIN_API static const InputSlot KeypadMinus;
  OXGN_PLIN_API static const InputSlot KeypadPlus;
  OXGN_PLIN_API static const InputSlot KeypadEnter;
  OXGN_PLIN_API static const InputSlot KeypadEquals;
  OXGN_PLIN_API static const InputSlot UpArrow;
  OXGN_PLIN_API static const InputSlot DownArrow;
  OXGN_PLIN_API static const InputSlot RightArrow;
  OXGN_PLIN_API static const InputSlot LeftArrow;
  OXGN_PLIN_API static const InputSlot Insert;
  OXGN_PLIN_API static const InputSlot Home;
  OXGN_PLIN_API static const InputSlot End;
  OXGN_PLIN_API static const InputSlot PageUp;
  OXGN_PLIN_API static const InputSlot PageDown;
  OXGN_PLIN_API static const InputSlot F1;
  OXGN_PLIN_API static const InputSlot F2;
  OXGN_PLIN_API static const InputSlot F3;
  OXGN_PLIN_API static const InputSlot F4;
  OXGN_PLIN_API static const InputSlot F5;
  OXGN_PLIN_API static const InputSlot F6;
  OXGN_PLIN_API static const InputSlot F7;
  OXGN_PLIN_API static const InputSlot F8;
  OXGN_PLIN_API static const InputSlot F9;
  OXGN_PLIN_API static const InputSlot F10;
  OXGN_PLIN_API static const InputSlot F11;
  OXGN_PLIN_API static const InputSlot F12;
  OXGN_PLIN_API static const InputSlot F13;
  OXGN_PLIN_API static const InputSlot F14;
  OXGN_PLIN_API static const InputSlot F15;
  OXGN_PLIN_API static const InputSlot Alpha0;
  OXGN_PLIN_API static const InputSlot Alpha1;
  OXGN_PLIN_API static const InputSlot Alpha2;
  OXGN_PLIN_API static const InputSlot Alpha3;
  OXGN_PLIN_API static const InputSlot Alpha4;
  OXGN_PLIN_API static const InputSlot Alpha5;
  OXGN_PLIN_API static const InputSlot Alpha6;
  OXGN_PLIN_API static const InputSlot Alpha7;
  OXGN_PLIN_API static const InputSlot Alpha8;
  OXGN_PLIN_API static const InputSlot Alpha9;
  OXGN_PLIN_API static const InputSlot Exclaim;
  OXGN_PLIN_API static const InputSlot DoubleQuote;
  OXGN_PLIN_API static const InputSlot Hash;
  OXGN_PLIN_API static const InputSlot Dollar;
  OXGN_PLIN_API static const InputSlot Percent;
  OXGN_PLIN_API static const InputSlot Ampersand;
  OXGN_PLIN_API static const InputSlot Quote;
  OXGN_PLIN_API static const InputSlot LeftParen;
  OXGN_PLIN_API static const InputSlot RightParen;
  OXGN_PLIN_API static const InputSlot Asterisk;
  OXGN_PLIN_API static const InputSlot Plus;
  OXGN_PLIN_API static const InputSlot Comma;
  OXGN_PLIN_API static const InputSlot Minus;
  OXGN_PLIN_API static const InputSlot Period;
  OXGN_PLIN_API static const InputSlot Slash;
  OXGN_PLIN_API static const InputSlot Colon;
  OXGN_PLIN_API static const InputSlot Semicolon;
  OXGN_PLIN_API static const InputSlot Less;
  OXGN_PLIN_API static const InputSlot Equals;
  OXGN_PLIN_API static const InputSlot Greater;
  OXGN_PLIN_API static const InputSlot Question;
  OXGN_PLIN_API static const InputSlot At;
  OXGN_PLIN_API static const InputSlot LeftBracket;
  OXGN_PLIN_API static const InputSlot Backslash;
  OXGN_PLIN_API static const InputSlot RightBracket;
  OXGN_PLIN_API static const InputSlot Caret;
  OXGN_PLIN_API static const InputSlot Underscore;
  OXGN_PLIN_API static const InputSlot BackQuote;
  OXGN_PLIN_API static const InputSlot A;
  OXGN_PLIN_API static const InputSlot B;
  OXGN_PLIN_API static const InputSlot C;
  OXGN_PLIN_API static const InputSlot D;
  OXGN_PLIN_API static const InputSlot E;
  OXGN_PLIN_API static const InputSlot F;
  OXGN_PLIN_API static const InputSlot G;
  OXGN_PLIN_API static const InputSlot H;
  OXGN_PLIN_API static const InputSlot I;
  OXGN_PLIN_API static const InputSlot J;
  OXGN_PLIN_API static const InputSlot K;
  OXGN_PLIN_API static const InputSlot L;
  OXGN_PLIN_API static const InputSlot M;
  OXGN_PLIN_API static const InputSlot N;
  OXGN_PLIN_API static const InputSlot O;
  OXGN_PLIN_API static const InputSlot P;
  OXGN_PLIN_API static const InputSlot Q;
  OXGN_PLIN_API static const InputSlot R;
  OXGN_PLIN_API static const InputSlot S;
  OXGN_PLIN_API static const InputSlot T;
  OXGN_PLIN_API static const InputSlot U;
  OXGN_PLIN_API static const InputSlot V;
  OXGN_PLIN_API static const InputSlot W;
  OXGN_PLIN_API static const InputSlot X;
  OXGN_PLIN_API static const InputSlot Y;
  OXGN_PLIN_API static const InputSlot Z;
  OXGN_PLIN_API static const InputSlot NumLock;
  OXGN_PLIN_API static const InputSlot CapsLock;
  OXGN_PLIN_API static const InputSlot ScrollLock;
  OXGN_PLIN_API static const InputSlot RightShift;
  OXGN_PLIN_API static const InputSlot LeftShift;
  OXGN_PLIN_API static const InputSlot RightControl;
  OXGN_PLIN_API static const InputSlot LeftControl;
  OXGN_PLIN_API static const InputSlot RightAlt;
  OXGN_PLIN_API static const InputSlot LeftAlt;
  OXGN_PLIN_API static const InputSlot LeftMeta;
  OXGN_PLIN_API static const InputSlot RightMeta;
  OXGN_PLIN_API static const InputSlot Help;
  OXGN_PLIN_API static const InputSlot Print;
  OXGN_PLIN_API static const InputSlot SysReq;
  OXGN_PLIN_API static const InputSlot Menu;
  // Mouse buttons
  // NOLINTEND

  friend class InputSlot;

  // TODO: review visibility and keep safe methods public
  // private:
  OXGN_PLIN_API static auto Initialize() -> void;

  OXGN_PLIN_API static auto GetAllInputSlots(std::vector<InputSlot>& out_keys)
    -> void;
  OXGN_PLIN_API static auto GetInputSlotForKey(Key key) -> InputSlot;

  OXGN_PLIN_API static auto GetCategoryDisplayName(
    std::string_view category_name) -> std::string_view;

  struct CategoryInfo {
    std::string_view display_string;
  };

  static std::map<InputSlot, std::shared_ptr<detail::InputSlotDetails>> slots_;
  static std::map<Key, InputSlot> key_slots_;
  static std::map<std::string_view, CategoryInfo> categories_;

  // TODO(abdes) add user defined slots and categories
  static auto AddCategory(
    std::string_view category_name, std::string_view display_string) -> void;
  static auto AddInputSlot(const detail::InputSlotDetails& details) -> void;
  static auto AddKeyInputSlot(
    Key key_code, const detail::InputSlotDetails& details) -> void;
  [[nodiscard]] static auto GetInputSlotDetails(const InputSlot& slot)
    -> std::shared_ptr<detail::InputSlotDetails>;
};

} // namespace oxygen::platform
