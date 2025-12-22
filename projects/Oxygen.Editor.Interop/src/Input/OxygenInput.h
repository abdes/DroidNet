//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#pragma managed(push, on)

#include <Views/ViewIdManaged.h>

using namespace System;
using namespace System::Numerics;

namespace Oxygen::Interop {
  ref class EngineContext;
}

namespace Oxygen::Interop::Input {

  /// <summary>
  /// Managed representation of the engine's platform::Key enum.
  /// Maps directly to engine key codes for type-safe interop.
  /// </summary>
  public
    enum class PlatformKey : int {
    None = 0,

    // Control keys
    BackSpace = 1,
    Delete = 2,
    Tab = 3,
    Clear = 4,
    Return = 5,
    Pause = 6,
    Escape = 7,
    Space = 8,

    // Keypad numbers
    Keypad0 = 9,
    Keypad1 = 10,
    Keypad2 = 11,
    Keypad3 = 12,
    Keypad4 = 13,
    Keypad5 = 14,
    Keypad6 = 15,
    Keypad7 = 16,
    Keypad8 = 17,
    Keypad9 = 18,
    KeypadPeriod = 19,
    KeypadDivide = 20,
    KeypadMultiply = 21,
    KeypadMinus = 22,
    KeypadPlus = 23,
    KeypadEnter = 24,
    KeypadEquals = 25,

    // Arrow keys
    UpArrow = 26,
    DownArrow = 27,
    RightArrow = 28,
    LeftArrow = 29,

    // Navigation
    Insert = 30,
    Home = 31,
    End = 32,
    PageUp = 33,
    PageDown = 34,

    // Function keys
    F1 = 35,
    F2 = 36,
    F3 = 37,
    F4 = 38,
    F5 = 39,
    F6 = 40,
    F7 = 41,
    F8 = 42,
    F9 = 43,
    F10 = 44,
    F11 = 45,
    F12 = 46,
    F13 = 47,
    F14 = 48,
    F15 = 49,

    // Number keys (top row)
    Alpha0 = 50,
    Alpha1 = 51,
    Alpha2 = 52,
    Alpha3 = 53,
    Alpha4 = 54,
    Alpha5 = 55,
    Alpha6 = 56,
    Alpha7 = 57,
    Alpha8 = 58,
    Alpha9 = 59,

    // Punctuation
    Exclaim = 60,
    DoubleQuote = 61,
    Hash = 62,
    Dollar = 63,
    Percent = 64,
    Ampersand = 65,
    Quote = 66,
    LeftParen = 67,
    RightParen = 68,
    Asterisk = 69,
    Plus = 70,
    Comma = 71,
    Minus = 72,
    Period = 73,
    Slash = 74,
    Colon = 75,
    Semicolon = 76,
    Less = 77,
    Equals = 78,
    Greater = 79,
    Question = 80,
    At = 81,
    LeftBracket = 82,
    Backslash = 83,
    RightBracket = 84,
    Caret = 85,
    Underscore = 86,
    BackQuote = 87,

    // Letter keys
    A = 88,
    B = 89,
    C = 90,
    D = 91,
    E = 92,
    F = 93,
    G = 94,
    H = 95,
    I = 96,
    J = 97,
    K = 98,
    L = 99,
    M = 100,
    N = 101,
    O = 102,
    P = 103,
    Q = 104,
    R = 105,
    S = 106,
    T = 107,
    U = 108,
    V = 109,
    W = 110,
    X = 111,
    Y = 112,
    Z = 113,

    LeftCurlyBracket = 114,
    Pipe = 115,
    RightCurlyBracket = 116,
    Tilde = 117,

    // Lock keys
    NumLock = 118,
    CapsLock = 119,
    ScrollLock = 120,

    // Modifiers
    RightShift = 121,
    LeftShift = 122,
    RightControl = 123,
    LeftControl = 124,
    RightAlt = 125,
    LeftAlt = 126,
    RightMeta = 127,
    LeftMeta = 128,

    // Special
    Help = 129,
    Print = 130,
    SysReq = 131,
    Menu = 132
  };

  /// <summary>
  /// Managed representation of the engine's platform::MouseButton enum.
  /// Maps directly to engine mouse button codes for type-safe interop.
  /// </summary>
  public
    enum class PlatformMouseButton : int {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 4,
    ExtButton1 = 8,
    ExtButton2 = 16
  };

  // Managed value-type event structs exposed to callers. These mirror the
  // native Editor*Event shapes but remain simple, blittable managed types
  // so callers can construct them easily.
  public
  value struct EditorKeyEventManaged {
    PlatformKey key;
    bool pressed;
    System::DateTime timestamp;
    System::Numerics::Vector2 position;
    bool repeat;
  };

  public
  value struct EditorButtonEventManaged {
    PlatformMouseButton button;
    bool pressed;
    System::DateTime timestamp;
    System::Numerics::Vector2 position;
  };

  public
  value struct EditorMouseMotionEventManaged {
    System::Numerics::Vector2 motion;
    System::Numerics::Vector2 position;
    System::DateTime timestamp;
  };

  public
  value struct EditorMouseWheelEventManaged {
    System::Numerics::Vector2 scroll;
    System::Numerics::Vector2 position;
    System::DateTime timestamp;
  };

  /// <summary>
  /// Managed bridge for engine input facilities. Forwards input from managed
  /// callers into the engine's EditorModule InputAccumulator.
  /// </summary>
  public
  ref class OxygenInput {
  public:
    explicit OxygenInput(EngineContext^ context);

    // Push a key event for the specified view. Timestamp is the managed
    // DateTime in UTC (preferred) — if MinValue is provided the current
    // timestamp will be used.
    void PushKeyEvent(Oxygen::Interop::ViewIdManaged viewId,
      EditorKeyEventManaged ev);

    // Push a mouse button event for the specified view.
    void PushButtonEvent(Oxygen::Interop::ViewIdManaged viewId,
      EditorButtonEventManaged ev);

    // Push mouse motion (delta + position)
    void PushMouseMotion(Oxygen::Interop::ViewIdManaged viewId,
      EditorMouseMotionEventManaged ev);

    // Push mouse wheel scroll (as motion + position)
    void PushMouseWheel(Oxygen::Interop::ViewIdManaged viewId,
      EditorMouseWheelEventManaged ev);

    // Notify input accumulator that focus was lost for a view
    void OnFocusLost(Oxygen::Interop::ViewIdManaged viewId);

  private:
    EngineContext^ context_;
  };

} // namespace Oxygen::Interop::Input

#pragma managed(pop)
