// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed RuntimeKey values; native mapping belongs to the Runtime adapter.</summary>
public enum RuntimeKey
{
    /// <summary>The None value.</summary>
    None = 0,

    /// <summary>The BackSpace value.</summary>
    BackSpace = 1,

    /// <summary>The Delete value.</summary>
    Delete = 2,

    /// <summary>The Tab value.</summary>
    Tab = 3,

    /// <summary>The Clear value.</summary>
    Clear = 4,

    /// <summary>The Return value.</summary>
    Return = 5,

    /// <summary>The Pause value.</summary>
    Pause = 6,

    /// <summary>The Escape value.</summary>
    Escape = 7,

    /// <summary>The Space value.</summary>
    Space = 8,

    /// <summary>The Keypad0 value.</summary>
    Keypad0 = 9,

    /// <summary>The Keypad1 value.</summary>
    Keypad1 = 10,

    /// <summary>The Keypad2 value.</summary>
    Keypad2 = 11,

    /// <summary>The Keypad3 value.</summary>
    Keypad3 = 12,

    /// <summary>The Keypad4 value.</summary>
    Keypad4 = 13,

    /// <summary>The Keypad5 value.</summary>
    Keypad5 = 14,

    /// <summary>The Keypad6 value.</summary>
    Keypad6 = 15,

    /// <summary>The Keypad7 value.</summary>
    Keypad7 = 16,

    /// <summary>The Keypad8 value.</summary>
    Keypad8 = 17,

    /// <summary>The Keypad9 value.</summary>
    Keypad9 = 18,

    /// <summary>The KeypadPeriod value.</summary>
    KeypadPeriod = 19,

    /// <summary>The KeypadDivide value.</summary>
    KeypadDivide = 20,

    /// <summary>The KeypadMultiply value.</summary>
    KeypadMultiply = 21,

    /// <summary>The KeypadMinus value.</summary>
    KeypadMinus = 22,

    /// <summary>The KeypadPlus value.</summary>
    KeypadPlus = 23,

    /// <summary>The KeypadEnter value.</summary>
    KeypadEnter = 24,

    /// <summary>The KeypadEquals value.</summary>
    KeypadEquals = 25,

    /// <summary>The UpArrow value.</summary>
    UpArrow = 26,

    /// <summary>The DownArrow value.</summary>
    DownArrow = 27,

    /// <summary>The RightArrow value.</summary>
    RightArrow = 28,

    /// <summary>The LeftArrow value.</summary>
    LeftArrow = 29,

    /// <summary>The Insert value.</summary>
    Insert = 30,

    /// <summary>The Home value.</summary>
    Home = 31,

    /// <summary>The End value.</summary>
    End = 32,

    /// <summary>The PageUp value.</summary>
    PageUp = 33,

    /// <summary>The PageDown value.</summary>
    PageDown = 34,

    /// <summary>The F1 value.</summary>
    F1 = 35,

    /// <summary>The F2 value.</summary>
    F2 = 36,

    /// <summary>The F3 value.</summary>
    F3 = 37,

    /// <summary>The F4 value.</summary>
    F4 = 38,

    /// <summary>The F5 value.</summary>
    F5 = 39,

    /// <summary>The F6 value.</summary>
    F6 = 40,

    /// <summary>The F7 value.</summary>
    F7 = 41,

    /// <summary>The F8 value.</summary>
    F8 = 42,

    /// <summary>The F9 value.</summary>
    F9 = 43,

    /// <summary>The F10 value.</summary>
    F10 = 44,

    /// <summary>The F11 value.</summary>
    F11 = 45,

    /// <summary>The F12 value.</summary>
    F12 = 46,

    /// <summary>The F13 value.</summary>
    F13 = 47,

    /// <summary>The F14 value.</summary>
    F14 = 48,

    /// <summary>The F15 value.</summary>
    F15 = 49,

    /// <summary>The Alpha0 value.</summary>
    Alpha0 = 50,

    /// <summary>The Alpha1 value.</summary>
    Alpha1 = 51,

    /// <summary>The Alpha2 value.</summary>
    Alpha2 = 52,

    /// <summary>The Alpha3 value.</summary>
    Alpha3 = 53,

    /// <summary>The Alpha4 value.</summary>
    Alpha4 = 54,

    /// <summary>The Alpha5 value.</summary>
    Alpha5 = 55,

    /// <summary>The Alpha6 value.</summary>
    Alpha6 = 56,

    /// <summary>The Alpha7 value.</summary>
    Alpha7 = 57,

    /// <summary>The Alpha8 value.</summary>
    Alpha8 = 58,

    /// <summary>The Alpha9 value.</summary>
    Alpha9 = 59,

    /// <summary>The Exclaim value.</summary>
    Exclaim = 60,

    /// <summary>The DoubleQuote value.</summary>
    DoubleQuote = 61,

    /// <summary>The Hash value.</summary>
    Hash = 62,

    /// <summary>The Dollar value.</summary>
    Dollar = 63,

    /// <summary>The Percent value.</summary>
    Percent = 64,

    /// <summary>The Ampersand value.</summary>
    Ampersand = 65,

    /// <summary>The Quote value.</summary>
    Quote = 66,

    /// <summary>The LeftParen value.</summary>
    LeftParen = 67,

    /// <summary>The RightParen value.</summary>
    RightParen = 68,

    /// <summary>The Asterisk value.</summary>
    Asterisk = 69,

    /// <summary>The Plus value.</summary>
    Plus = 70,

    /// <summary>The Comma value.</summary>
    Comma = 71,

    /// <summary>The Minus value.</summary>
    Minus = 72,

    /// <summary>The Period value.</summary>
    Period = 73,

    /// <summary>The Slash value.</summary>
    Slash = 74,

    /// <summary>The Colon value.</summary>
    Colon = 75,

    /// <summary>The Semicolon value.</summary>
    Semicolon = 76,

    /// <summary>The Less value.</summary>
    Less = 77,

    /// <summary>The Equals value.</summary>
    Equals = 78,

    /// <summary>The Greater value.</summary>
    Greater = 79,

    /// <summary>The Question value.</summary>
    Question = 80,

    /// <summary>The At value.</summary>
    At = 81,

    /// <summary>The LeftBracket value.</summary>
    LeftBracket = 82,

    /// <summary>The Backslash value.</summary>
    Backslash = 83,

    /// <summary>The RightBracket value.</summary>
    RightBracket = 84,

    /// <summary>The Caret value.</summary>
    Caret = 85,

    /// <summary>The Underscore value.</summary>
    Underscore = 86,

    /// <summary>The BackQuote value.</summary>
    BackQuote = 87,

    /// <summary>The A value.</summary>
    A = 88,

    /// <summary>The B value.</summary>
    B = 89,

    /// <summary>The C value.</summary>
    C = 90,

    /// <summary>The D value.</summary>
    D = 91,

    /// <summary>The E value.</summary>
    E = 92,

    /// <summary>The F value.</summary>
    F = 93,

    /// <summary>The G value.</summary>
    G = 94,

    /// <summary>The H value.</summary>
    H = 95,

    /// <summary>The I value.</summary>
    I = 96,

    /// <summary>The J value.</summary>
    J = 97,

    /// <summary>The K value.</summary>
    K = 98,

    /// <summary>The L value.</summary>
    L = 99,

    /// <summary>The M value.</summary>
    M = 100,

    /// <summary>The N value.</summary>
    N = 101,

    /// <summary>The O value.</summary>
    O = 102,

    /// <summary>The P value.</summary>
    P = 103,

    /// <summary>The Q value.</summary>
    Q = 104,

    /// <summary>The R value.</summary>
    R = 105,

    /// <summary>The S value.</summary>
    S = 106,

    /// <summary>The T value.</summary>
    T = 107,

    /// <summary>The U value.</summary>
    U = 108,

    /// <summary>The V value.</summary>
    V = 109,

    /// <summary>The W value.</summary>
    W = 110,

    /// <summary>The X value.</summary>
    X = 111,

    /// <summary>The Y value.</summary>
    Y = 112,

    /// <summary>The Z value.</summary>
    Z = 113,

    /// <summary>The LeftCurlyBracket value.</summary>
    LeftCurlyBracket = 114,

    /// <summary>The Pipe value.</summary>
    Pipe = 115,

    /// <summary>The RightCurlyBracket value.</summary>
    RightCurlyBracket = 116,

    /// <summary>The Tilde value.</summary>
    Tilde = 117,

    /// <summary>The NumLock value.</summary>
    NumLock = 118,

    /// <summary>The CapsLock value.</summary>
    CapsLock = 119,

    /// <summary>The ScrollLock value.</summary>
    ScrollLock = 120,

    /// <summary>The RightShift value.</summary>
    RightShift = 121,

    /// <summary>The LeftShift value.</summary>
    LeftShift = 122,

    /// <summary>The RightControl value.</summary>
    RightControl = 123,

    /// <summary>The LeftControl value.</summary>
    LeftControl = 124,

    /// <summary>The RightAlt value.</summary>
    RightAlt = 125,

    /// <summary>The LeftAlt value.</summary>
    LeftAlt = 126,

    /// <summary>The RightMeta value.</summary>
    RightMeta = 127,

    /// <summary>The LeftMeta value.</summary>
    LeftMeta = 128,

    /// <summary>The Help value.</summary>
    Help = 129,

    /// <summary>The Print value.</summary>
    Print = 130,

    /// <summary>The SysReq value.</summary>
    SysReq = 131,

    /// <summary>The Menu value.</summary>
    Menu = 132,
}
