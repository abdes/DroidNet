// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop.Input;
using Microsoft.UI.Input;
using Windows.System;

namespace Oxygen.Editor.Runtime.Input;

/// <summary>
/// Translates WinUI input types to engine platform input types.
/// Provides clean, type-safe translation from WinUI events to engine-compatible enums.
/// </summary>
public static class InputTranslation
{
    /// <summary>
    /// Translates a WinUI VirtualKey (with optional modifiers or produced character)
    /// to the engine's PlatformKey enum.
    /// </summary>
    /// <param name="key">The WinUI VirtualKey to translate.</param>
    /// <param name="modifiers">Modifier keys state (e.g. Shift) when available.</param>
    /// <param name="character">Optional produced character (preferred for punctuation/shifted keys).</param>
    /// <returns>The corresponding engine PlatformKey value.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "code is clear if all keys are in the same place")]
    public static PlatformKey TranslateKey(VirtualKey key, VirtualKeyModifiers modifiers = VirtualKeyModifiers.None, char? character = null)
    {
        // If we have an actual produced character (from text input), prefer it for exact mapping
        if (character.HasValue)
        {
            switch (character.Value)
            {
                // Letters
                case 'a': case 'A': return PlatformKey.A;
                case 'b': case 'B': return PlatformKey.B;
                case 'c': case 'C': return PlatformKey.C;
                case 'd': case 'D': return PlatformKey.D;
                case 'e': case 'E': return PlatformKey.E;
                case 'f': case 'F': return PlatformKey.F;
                case 'g': case 'G': return PlatformKey.G;
                case 'h': case 'H': return PlatformKey.H;
                case 'i': case 'I': return PlatformKey.I;
                case 'j': case 'J': return PlatformKey.J;
                case 'k': case 'K': return PlatformKey.K;
                case 'l': case 'L': return PlatformKey.L;
                case 'm': case 'M': return PlatformKey.M;
                case 'n': case 'N': return PlatformKey.N;
                case 'o': case 'O': return PlatformKey.O;
                case 'p': case 'P': return PlatformKey.P;
                case 'q': case 'Q': return PlatformKey.Q;
                case 'r': case 'R': return PlatformKey.R;
                case 's': case 'S': return PlatformKey.S;
                case 't': case 'T': return PlatformKey.T;
                case 'u': case 'U': return PlatformKey.U;
                case 'v': case 'V': return PlatformKey.V;
                case 'w': case 'W': return PlatformKey.W;
                case 'x': case 'X': return PlatformKey.X;
                case 'y': case 'Y': return PlatformKey.Y;
                case 'z': case 'Z': return PlatformKey.Z;

                // Numbers (main row)
                case '0': return PlatformKey.Alpha0;
                case '1': return PlatformKey.Alpha1;
                case '2': return PlatformKey.Alpha2;
                case '3': return PlatformKey.Alpha3;
                case '4': return PlatformKey.Alpha4;
                case '5': return PlatformKey.Alpha5;
                case '6': return PlatformKey.Alpha6;
                case '7': return PlatformKey.Alpha7;
                case '8': return PlatformKey.Alpha8;
                case '9': return PlatformKey.Alpha9;

                // Punctuation / shifted characters
                case '!': return PlatformKey.Exclaim;
                case '"': return PlatformKey.DoubleQuote;
                case '#': return PlatformKey.Hash;
                case '$': return PlatformKey.Dollar;
                case '%': return PlatformKey.Percent;
                case '&': return PlatformKey.Ampersand;
                case '\'': return PlatformKey.Quote;
                case '(': return PlatformKey.LeftParen;
                case ')': return PlatformKey.RightParen;
                case '*': return PlatformKey.Asterisk;
                case '+': return PlatformKey.Plus;
                case ',': return PlatformKey.Comma;
                case '-': return PlatformKey.Minus;
                case '.': return PlatformKey.Period;
                case '/': return PlatformKey.Slash;
                case ':': return PlatformKey.Colon;
                case ';': return PlatformKey.Semicolon;
                case '<': return PlatformKey.Less;
                case '=': return PlatformKey.Equals;
                case '>': return PlatformKey.Greater;
                case '?': return PlatformKey.Question;
                case '@': return PlatformKey.At;
                case '[': return PlatformKey.LeftBracket;
                case '\\': return PlatformKey.Backslash;
                case ']': return PlatformKey.RightBracket;
                case '^': return PlatformKey.Caret;
                case '_': return PlatformKey.Underscore;
                case '`': return PlatformKey.BackQuote;
                case '{': return PlatformKey.LeftCurlyBracket;
                case '|': return PlatformKey.Pipe;
                case '}': return PlatformKey.RightCurlyBracket;
                case '~': return PlatformKey.Tilde;

                // Fallback - if char is not recognized let key-based mapping handle it
                default: break;
            }
        }

        // If we got here, either no character was provided or we couldn't map it.
        // Try to map using VirtualKey and modifier state (handle common shifted cases).
        // Handle common Shift+number US-layout mappings when character is not supplied.
        var shift = (modifiers & VirtualKeyModifiers.Shift) == VirtualKeyModifiers.Shift;
        if (shift)
        {
            switch (key)
            {
                case VirtualKey.Number1: return PlatformKey.Exclaim; // '!'
                case VirtualKey.Number2: return PlatformKey.At;      // '@'
                case VirtualKey.Number3: return PlatformKey.Hash;    // '#'
                case VirtualKey.Number4: return PlatformKey.Dollar;  // '$'
                case VirtualKey.Number5: return PlatformKey.Percent; // '%'
                case VirtualKey.Number6: return PlatformKey.Caret;   // '^'
                case VirtualKey.Number7: return PlatformKey.Ampersand; // '&'
                case VirtualKey.Number8: return PlatformKey.Asterisk; // '*'
                case VirtualKey.Number9: return PlatformKey.LeftParen; // '('
                case VirtualKey.Number0: return PlatformKey.RightParen; // ')'
                default: break;
            }
        }

        // Fall back to VirtualKey mapping (covers letters, keypad, arrows, function keys, etc.)
        return key switch
        {
            // Alphabetic keys (A-Z)
            VirtualKey.A => PlatformKey.A,
            VirtualKey.B => PlatformKey.B,
            VirtualKey.C => PlatformKey.C,
            VirtualKey.D => PlatformKey.D,
            VirtualKey.E => PlatformKey.E,
            VirtualKey.F => PlatformKey.F,
            VirtualKey.G => PlatformKey.G,
            VirtualKey.H => PlatformKey.H,
            VirtualKey.I => PlatformKey.I,
            VirtualKey.J => PlatformKey.J,
            VirtualKey.K => PlatformKey.K,
            VirtualKey.L => PlatformKey.L,
            VirtualKey.M => PlatformKey.M,
            VirtualKey.N => PlatformKey.N,
            VirtualKey.O => PlatformKey.O,
            VirtualKey.P => PlatformKey.P,
            VirtualKey.Q => PlatformKey.Q,
            VirtualKey.R => PlatformKey.R,
            VirtualKey.S => PlatformKey.S,
            VirtualKey.T => PlatformKey.T,
            VirtualKey.U => PlatformKey.U,
            VirtualKey.V => PlatformKey.V,
            VirtualKey.W => PlatformKey.W,
            VirtualKey.X => PlatformKey.X,
            VirtualKey.Y => PlatformKey.Y,
            VirtualKey.Z => PlatformKey.Z,

            // Number keys (0-9) - main keyboard
            VirtualKey.Number0 => PlatformKey.Alpha0,
            VirtualKey.Number1 => PlatformKey.Alpha1,
            VirtualKey.Number2 => PlatformKey.Alpha2,
            VirtualKey.Number3 => PlatformKey.Alpha3,
            VirtualKey.Number4 => PlatformKey.Alpha4,
            VirtualKey.Number5 => PlatformKey.Alpha5,
            VirtualKey.Number6 => PlatformKey.Alpha6,
            VirtualKey.Number7 => PlatformKey.Alpha7,
            VirtualKey.Number8 => PlatformKey.Alpha8,
            VirtualKey.Number9 => PlatformKey.Alpha9,

            // Function keys (F1-F15)
            VirtualKey.F1 => PlatformKey.F1,
            VirtualKey.F2 => PlatformKey.F2,
            VirtualKey.F3 => PlatformKey.F3,
            VirtualKey.F4 => PlatformKey.F4,
            VirtualKey.F5 => PlatformKey.F5,
            VirtualKey.F6 => PlatformKey.F6,
            VirtualKey.F7 => PlatformKey.F7,
            VirtualKey.F8 => PlatformKey.F8,
            VirtualKey.F9 => PlatformKey.F9,
            VirtualKey.F10 => PlatformKey.F10,
            VirtualKey.F11 => PlatformKey.F11,
            VirtualKey.F12 => PlatformKey.F12,
            VirtualKey.F13 => PlatformKey.F13,
            VirtualKey.F14 => PlatformKey.F14,
            VirtualKey.F15 => PlatformKey.F15,

            // Numeric keypad (0-9)
            VirtualKey.NumberPad0 => PlatformKey.Keypad0,
            VirtualKey.NumberPad1 => PlatformKey.Keypad1,
            VirtualKey.NumberPad2 => PlatformKey.Keypad2,
            VirtualKey.NumberPad3 => PlatformKey.Keypad3,
            VirtualKey.NumberPad4 => PlatformKey.Keypad4,
            VirtualKey.NumberPad5 => PlatformKey.Keypad5,
            VirtualKey.NumberPad6 => PlatformKey.Keypad6,
            VirtualKey.NumberPad7 => PlatformKey.Keypad7,
            VirtualKey.NumberPad8 => PlatformKey.Keypad8,
            VirtualKey.NumberPad9 => PlatformKey.Keypad9,

            // Keypad operators
            VirtualKey.Multiply => PlatformKey.KeypadMultiply,
            VirtualKey.Add => PlatformKey.KeypadPlus,
            VirtualKey.Subtract => PlatformKey.KeypadMinus,
            VirtualKey.Decimal => PlatformKey.KeypadPeriod,
            VirtualKey.Divide => PlatformKey.KeypadDivide,

            // Arrow keys
            VirtualKey.Up => PlatformKey.UpArrow,
            VirtualKey.Down => PlatformKey.DownArrow,
            VirtualKey.Left => PlatformKey.LeftArrow,
            VirtualKey.Right => PlatformKey.RightArrow,

            // Navigation keys
            VirtualKey.Home => PlatformKey.Home,
            VirtualKey.End => PlatformKey.End,
            VirtualKey.PageUp => PlatformKey.PageUp,
            VirtualKey.PageDown => PlatformKey.PageDown,
            VirtualKey.Insert => PlatformKey.Insert,
            VirtualKey.Delete => PlatformKey.Delete,

            // Modifier keys
            VirtualKey.Shift => PlatformKey.LeftShift,
            VirtualKey.Control => PlatformKey.LeftControl,
            VirtualKey.Menu => PlatformKey.LeftAlt,
            VirtualKey.LeftShift => PlatformKey.LeftShift,
            VirtualKey.RightShift => PlatformKey.RightShift,
            VirtualKey.LeftControl => PlatformKey.LeftControl,
            VirtualKey.RightControl => PlatformKey.RightControl,
            VirtualKey.LeftMenu => PlatformKey.LeftAlt,
            VirtualKey.RightMenu => PlatformKey.RightAlt,
            VirtualKey.LeftWindows => PlatformKey.LeftMeta,
            VirtualKey.RightWindows => PlatformKey.RightMeta,

            // Lock keys
            VirtualKey.CapitalLock => PlatformKey.CapsLock,
            VirtualKey.NumberKeyLock => PlatformKey.NumLock,
            VirtualKey.Scroll => PlatformKey.ScrollLock,

            // Control keys
            VirtualKey.Space => PlatformKey.Space,
            VirtualKey.Enter => PlatformKey.Return,
            VirtualKey.Tab => PlatformKey.Tab,
            VirtualKey.Back => PlatformKey.BackSpace,
            VirtualKey.Escape => PlatformKey.Escape,

            // Special keys
            VirtualKey.Pause => PlatformKey.Pause,
            VirtualKey.Clear => PlatformKey.Clear,
            VirtualKey.Print => PlatformKey.Print,
            VirtualKey.Help => PlatformKey.Help,
            VirtualKey.Application => PlatformKey.Menu,

            // Unsupported keys
            _ => PlatformKey.None,
        };
    }

    /// <summary>
    /// Determines which mouse button is pressed from pointer properties.
    /// </summary>
    /// <param name="properties">The pointer point properties from a WinUI pointer event.</param>
    /// <returns>The corresponding engine PlatformMouseButton value.</returns>
    public static PlatformMouseButton TranslateMouseButton(PointerPointProperties properties)
        => properties switch
        {
            { IsLeftButtonPressed: true } => PlatformMouseButton.Left,
            { IsRightButtonPressed: true } => PlatformMouseButton.Right,
            { IsMiddleButtonPressed: true } => PlatformMouseButton.Middle,
            { IsXButton1Pressed: true } => PlatformMouseButton.ExtButton1,
            { IsXButton2Pressed: true } => PlatformMouseButton.ExtButton2,
            _ => PlatformMouseButton.None,
        };
}
