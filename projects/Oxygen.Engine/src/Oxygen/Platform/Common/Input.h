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

#include "Oxygen/Platform/Common/Types.h"
#include "Oxygen/Platform/Common/api_export.h"

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
    kNone = 0, // Unsupported mouse button that should never be transmitted in an event.

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

    OXYGEN_PLATFORM_API auto IsModifierKey() const -> bool;
    OXYGEN_PLATFORM_API auto IsKeyboardKey() const -> bool;
    OXYGEN_PLATFORM_API auto IsMouseButton() const -> bool;

    OXYGEN_PLATFORM_API auto IsAxis1D() const -> bool;
    OXYGEN_PLATFORM_API auto IsAxis2D() const -> bool;
    OXYGEN_PLATFORM_API auto IsAxis3D() const -> bool;

    friend class InputSlots;

    OXYGEN_PLATFORM_API
    [[nodiscard]] auto GetDisplayString() const -> std::string_view;

    OXYGEN_PLATFORM_API
    [[nodiscard]] auto GetInputCategoryName() const -> std::string_view;

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

    void UpdateDetailsIfNotUpdated() const;
};
} // namespace oxygen::platform

template <>
struct std::hash<oxygen::platform::InputSlot> {
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
    OXYGEN_PLATFORM_API static const std::string_view kKeyCategoryName;
    OXYGEN_PLATFORM_API static const std::string_view kMouseCategoryName;

    // -- Static input slots
    // NOLINTBEGIN
    // Mouse slots
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelUp;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelDown;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelLeft;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelRight;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelX;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelY;
    OXYGEN_PLATFORM_API static const InputSlot MouseWheelXY;
    OXYGEN_PLATFORM_API static const InputSlot LeftMouseButton;
    OXYGEN_PLATFORM_API static const InputSlot RightMouseButton;
    OXYGEN_PLATFORM_API static const InputSlot MiddleMouseButton;
    OXYGEN_PLATFORM_API static const InputSlot ThumbMouseButton1;
    OXYGEN_PLATFORM_API static const InputSlot ThumbMouseButton2;
    OXYGEN_PLATFORM_API static const InputSlot MouseX;
    OXYGEN_PLATFORM_API static const InputSlot MouseY;
    OXYGEN_PLATFORM_API static const InputSlot MouseXY;

    // Keyboard slots
    OXYGEN_PLATFORM_API static const InputSlot None;
    OXYGEN_PLATFORM_API static const InputSlot AnyKey;
    OXYGEN_PLATFORM_API static const InputSlot BackSpace;
    OXYGEN_PLATFORM_API static const InputSlot Delete;
    OXYGEN_PLATFORM_API static const InputSlot Tab;
    OXYGEN_PLATFORM_API static const InputSlot Clear;
    OXYGEN_PLATFORM_API static const InputSlot Return;
    OXYGEN_PLATFORM_API static const InputSlot Pause;
    OXYGEN_PLATFORM_API static const InputSlot Escape;
    OXYGEN_PLATFORM_API static const InputSlot Space;
    OXYGEN_PLATFORM_API static const InputSlot Keypad0;
    OXYGEN_PLATFORM_API static const InputSlot Keypad1;
    OXYGEN_PLATFORM_API static const InputSlot Keypad2;
    OXYGEN_PLATFORM_API static const InputSlot Keypad3;
    OXYGEN_PLATFORM_API static const InputSlot Keypad4;
    OXYGEN_PLATFORM_API static const InputSlot Keypad5;
    OXYGEN_PLATFORM_API static const InputSlot Keypad6;
    OXYGEN_PLATFORM_API static const InputSlot Keypad7;
    OXYGEN_PLATFORM_API static const InputSlot Keypad8;
    OXYGEN_PLATFORM_API static const InputSlot Keypad9;
    OXYGEN_PLATFORM_API static const InputSlot KeypadPeriod;
    OXYGEN_PLATFORM_API static const InputSlot KeypadDivide;
    OXYGEN_PLATFORM_API static const InputSlot KeypadMultiply;
    OXYGEN_PLATFORM_API static const InputSlot KeypadMinus;
    OXYGEN_PLATFORM_API static const InputSlot KeypadPlus;
    OXYGEN_PLATFORM_API static const InputSlot KeypadEnter;
    OXYGEN_PLATFORM_API static const InputSlot KeypadEquals;
    OXYGEN_PLATFORM_API static const InputSlot UpArrow;
    OXYGEN_PLATFORM_API static const InputSlot DownArrow;
    OXYGEN_PLATFORM_API static const InputSlot RightArrow;
    OXYGEN_PLATFORM_API static const InputSlot LeftArrow;
    OXYGEN_PLATFORM_API static const InputSlot Insert;
    OXYGEN_PLATFORM_API static const InputSlot Home;
    OXYGEN_PLATFORM_API static const InputSlot End;
    OXYGEN_PLATFORM_API static const InputSlot PageUp;
    OXYGEN_PLATFORM_API static const InputSlot PageDown;
    OXYGEN_PLATFORM_API static const InputSlot F1;
    OXYGEN_PLATFORM_API static const InputSlot F2;
    OXYGEN_PLATFORM_API static const InputSlot F3;
    OXYGEN_PLATFORM_API static const InputSlot F4;
    OXYGEN_PLATFORM_API static const InputSlot F5;
    OXYGEN_PLATFORM_API static const InputSlot F6;
    OXYGEN_PLATFORM_API static const InputSlot F7;
    OXYGEN_PLATFORM_API static const InputSlot F8;
    OXYGEN_PLATFORM_API static const InputSlot F9;
    OXYGEN_PLATFORM_API static const InputSlot F10;
    OXYGEN_PLATFORM_API static const InputSlot F11;
    OXYGEN_PLATFORM_API static const InputSlot F12;
    OXYGEN_PLATFORM_API static const InputSlot F13;
    OXYGEN_PLATFORM_API static const InputSlot F14;
    OXYGEN_PLATFORM_API static const InputSlot F15;
    OXYGEN_PLATFORM_API static const InputSlot Alpha0;
    OXYGEN_PLATFORM_API static const InputSlot Alpha1;
    OXYGEN_PLATFORM_API static const InputSlot Alpha2;
    OXYGEN_PLATFORM_API static const InputSlot Alpha3;
    OXYGEN_PLATFORM_API static const InputSlot Alpha4;
    OXYGEN_PLATFORM_API static const InputSlot Alpha5;
    OXYGEN_PLATFORM_API static const InputSlot Alpha6;
    OXYGEN_PLATFORM_API static const InputSlot Alpha7;
    OXYGEN_PLATFORM_API static const InputSlot Alpha8;
    OXYGEN_PLATFORM_API static const InputSlot Alpha9;
    OXYGEN_PLATFORM_API static const InputSlot Exclaim;
    OXYGEN_PLATFORM_API static const InputSlot DoubleQuote;
    OXYGEN_PLATFORM_API static const InputSlot Hash;
    OXYGEN_PLATFORM_API static const InputSlot Dollar;
    OXYGEN_PLATFORM_API static const InputSlot Percent;
    OXYGEN_PLATFORM_API static const InputSlot Ampersand;
    OXYGEN_PLATFORM_API static const InputSlot Quote;
    OXYGEN_PLATFORM_API static const InputSlot LeftParen;
    OXYGEN_PLATFORM_API static const InputSlot RightParen;
    OXYGEN_PLATFORM_API static const InputSlot Asterisk;
    OXYGEN_PLATFORM_API static const InputSlot Plus;
    OXYGEN_PLATFORM_API static const InputSlot Comma;
    OXYGEN_PLATFORM_API static const InputSlot Minus;
    OXYGEN_PLATFORM_API static const InputSlot Period;
    OXYGEN_PLATFORM_API static const InputSlot Slash;
    OXYGEN_PLATFORM_API static const InputSlot Colon;
    OXYGEN_PLATFORM_API static const InputSlot Semicolon;
    OXYGEN_PLATFORM_API static const InputSlot Less;
    OXYGEN_PLATFORM_API static const InputSlot Equals;
    OXYGEN_PLATFORM_API static const InputSlot Greater;
    OXYGEN_PLATFORM_API static const InputSlot Question;
    OXYGEN_PLATFORM_API static const InputSlot At;
    OXYGEN_PLATFORM_API static const InputSlot LeftBracket;
    OXYGEN_PLATFORM_API static const InputSlot Backslash;
    OXYGEN_PLATFORM_API static const InputSlot RightBracket;
    OXYGEN_PLATFORM_API static const InputSlot Caret;
    OXYGEN_PLATFORM_API static const InputSlot Underscore;
    OXYGEN_PLATFORM_API static const InputSlot BackQuote;
    OXYGEN_PLATFORM_API static const InputSlot A;
    OXYGEN_PLATFORM_API static const InputSlot B;
    OXYGEN_PLATFORM_API static const InputSlot C;
    OXYGEN_PLATFORM_API static const InputSlot D;
    OXYGEN_PLATFORM_API static const InputSlot E;
    OXYGEN_PLATFORM_API static const InputSlot F;
    OXYGEN_PLATFORM_API static const InputSlot G;
    OXYGEN_PLATFORM_API static const InputSlot H;
    OXYGEN_PLATFORM_API static const InputSlot I;
    OXYGEN_PLATFORM_API static const InputSlot J;
    OXYGEN_PLATFORM_API static const InputSlot K;
    OXYGEN_PLATFORM_API static const InputSlot L;
    OXYGEN_PLATFORM_API static const InputSlot M;
    OXYGEN_PLATFORM_API static const InputSlot N;
    OXYGEN_PLATFORM_API static const InputSlot O;
    OXYGEN_PLATFORM_API static const InputSlot P;
    OXYGEN_PLATFORM_API static const InputSlot Q;
    OXYGEN_PLATFORM_API static const InputSlot R;
    OXYGEN_PLATFORM_API static const InputSlot S;
    OXYGEN_PLATFORM_API static const InputSlot T;
    OXYGEN_PLATFORM_API static const InputSlot U;
    OXYGEN_PLATFORM_API static const InputSlot V;
    OXYGEN_PLATFORM_API static const InputSlot W;
    OXYGEN_PLATFORM_API static const InputSlot X;
    OXYGEN_PLATFORM_API static const InputSlot Y;
    OXYGEN_PLATFORM_API static const InputSlot Z;
    OXYGEN_PLATFORM_API static const InputSlot NumLock;
    OXYGEN_PLATFORM_API static const InputSlot CapsLock;
    OXYGEN_PLATFORM_API static const InputSlot ScrollLock;
    OXYGEN_PLATFORM_API static const InputSlot RightShift;
    OXYGEN_PLATFORM_API static const InputSlot LeftShift;
    OXYGEN_PLATFORM_API static const InputSlot RightControl;
    OXYGEN_PLATFORM_API static const InputSlot LeftControl;
    OXYGEN_PLATFORM_API static const InputSlot RightAlt;
    OXYGEN_PLATFORM_API static const InputSlot LeftAlt;
    OXYGEN_PLATFORM_API static const InputSlot LeftMeta;
    OXYGEN_PLATFORM_API static const InputSlot RightMeta;
    OXYGEN_PLATFORM_API static const InputSlot Help;
    OXYGEN_PLATFORM_API static const InputSlot Print;
    OXYGEN_PLATFORM_API static const InputSlot SysReq;
    OXYGEN_PLATFORM_API static const InputSlot Menu;
    // Mouse buttons
    // NOLINTEND

    friend class oxygen::Platform;
    friend class InputSlot;

    // TODO: review visibility and keep safe methods public
    // private:
    OXYGEN_PLATFORM_API static void Initialize();

    OXYGEN_PLATFORM_API static void GetAllInputSlots(std::vector<InputSlot>& out_keys);
    OXYGEN_PLATFORM_API static auto GetInputSlotForKey(Key key) -> InputSlot;

    OXYGEN_PLATFORM_API static auto GetCategoryDisplayName(std::string_view category_name)
        -> std::string_view;

    struct CategoryInfo {
        std::string_view display_string;
    };

    static std::map<InputSlot, std::shared_ptr<detail::InputSlotDetails>> slots_;
    static std::map<Key, InputSlot> key_slots_;
    static std::map<std::string_view, CategoryInfo> categories_;

    // TODO(abdes) add user defined slots and categories
    static void AddCategory(std::string_view category_name,
        std::string_view display_string);
    static void AddInputSlot(const detail::InputSlotDetails& detail);
    static void AddKeyInputSlot(Key key_code,
        const detail::InputSlotDetails& detail);
    [[nodiscard]] static auto GetInputSlotDetails(const InputSlot& slot)
        -> std::shared_ptr<detail::InputSlotDetails>;
};

} // namespace oxygen::platform
