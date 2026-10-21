// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Oxygen.Editor.Runtime.Engine;
using Windows.System;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Translates WinUI input types to engine platform input types.
/// Provides clean, type-safe translation from WinUI events to engine-compatible enums.
/// </summary>
internal static class InputTranslation
{
    /// <summary>
    /// Translates a WinUI VirtualKey (with optional modifiers or produced character)
    /// to the engine's RuntimeKey enum.
    /// </summary>
    /// <param name="key">The WinUI VirtualKey to translate.</param>
    /// <param name="modifiers">Modifier keys state (e.g. Shift) when available.</param>
    /// <param name="character">Optional produced character (preferred for punctuation/shifted keys).</param>
    /// <returns>The corresponding engine RuntimeKey value.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "code is clear if all keys are in the same place")]
    public static RuntimeKey TranslateKey(VirtualKey key, VirtualKeyModifiers modifiers = VirtualKeyModifiers.None, char? character = null)
    {
        // If we have an actual produced character (from text input), prefer it for exact mapping
        if (character.HasValue)
        {
            switch (character.Value)
            {
                // Letters
                case 'a': case 'A': return RuntimeKey.A;
                case 'b': case 'B': return RuntimeKey.B;
                case 'c': case 'C': return RuntimeKey.C;
                case 'd': case 'D': return RuntimeKey.D;
                case 'e': case 'E': return RuntimeKey.E;
                case 'f': case 'F': return RuntimeKey.F;
                case 'g': case 'G': return RuntimeKey.G;
                case 'h': case 'H': return RuntimeKey.H;
                case 'i': case 'I': return RuntimeKey.I;
                case 'j': case 'J': return RuntimeKey.J;
                case 'k': case 'K': return RuntimeKey.K;
                case 'l': case 'L': return RuntimeKey.L;
                case 'm': case 'M': return RuntimeKey.M;
                case 'n': case 'N': return RuntimeKey.N;
                case 'o': case 'O': return RuntimeKey.O;
                case 'p': case 'P': return RuntimeKey.P;
                case 'q': case 'Q': return RuntimeKey.Q;
                case 'r': case 'R': return RuntimeKey.R;
                case 's': case 'S': return RuntimeKey.S;
                case 't': case 'T': return RuntimeKey.T;
                case 'u': case 'U': return RuntimeKey.U;
                case 'v': case 'V': return RuntimeKey.V;
                case 'w': case 'W': return RuntimeKey.W;
                case 'x': case 'X': return RuntimeKey.X;
                case 'y': case 'Y': return RuntimeKey.Y;
                case 'z': case 'Z': return RuntimeKey.Z;

                // Numbers (main row)
                case '0': return RuntimeKey.Alpha0;
                case '1': return RuntimeKey.Alpha1;
                case '2': return RuntimeKey.Alpha2;
                case '3': return RuntimeKey.Alpha3;
                case '4': return RuntimeKey.Alpha4;
                case '5': return RuntimeKey.Alpha5;
                case '6': return RuntimeKey.Alpha6;
                case '7': return RuntimeKey.Alpha7;
                case '8': return RuntimeKey.Alpha8;
                case '9': return RuntimeKey.Alpha9;

                // Punctuation / shifted characters
                case '!': return RuntimeKey.Exclaim;
                case '"': return RuntimeKey.DoubleQuote;
                case '#': return RuntimeKey.Hash;
                case '$': return RuntimeKey.Dollar;
                case '%': return RuntimeKey.Percent;
                case '&': return RuntimeKey.Ampersand;
                case '\'': return RuntimeKey.Quote;
                case '(': return RuntimeKey.LeftParen;
                case ')': return RuntimeKey.RightParen;
                case '*': return RuntimeKey.Asterisk;
                case '+': return RuntimeKey.Plus;
                case ',': return RuntimeKey.Comma;
                case '-': return RuntimeKey.Minus;
                case '.': return RuntimeKey.Period;
                case '/': return RuntimeKey.Slash;
                case ':': return RuntimeKey.Colon;
                case ';': return RuntimeKey.Semicolon;
                case '<': return RuntimeKey.Less;
                case '=': return RuntimeKey.Equals;
                case '>': return RuntimeKey.Greater;
                case '?': return RuntimeKey.Question;
                case '@': return RuntimeKey.At;
                case '[': return RuntimeKey.LeftBracket;
                case '\\': return RuntimeKey.Backslash;
                case ']': return RuntimeKey.RightBracket;
                case '^': return RuntimeKey.Caret;
                case '_': return RuntimeKey.Underscore;
                case '`': return RuntimeKey.BackQuote;
                case '{': return RuntimeKey.LeftCurlyBracket;
                case '|': return RuntimeKey.Pipe;
                case '}': return RuntimeKey.RightCurlyBracket;
                case '~': return RuntimeKey.Tilde;

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
                case VirtualKey.Number1: return RuntimeKey.Exclaim; // '!'
                case VirtualKey.Number2: return RuntimeKey.At;      // '@'
                case VirtualKey.Number3: return RuntimeKey.Hash;    // '#'
                case VirtualKey.Number4: return RuntimeKey.Dollar;  // '$'
                case VirtualKey.Number5: return RuntimeKey.Percent; // '%'
                case VirtualKey.Number6: return RuntimeKey.Caret;   // '^'
                case VirtualKey.Number7: return RuntimeKey.Ampersand; // '&'
                case VirtualKey.Number8: return RuntimeKey.Asterisk; // '*'
                case VirtualKey.Number9: return RuntimeKey.LeftParen; // '('
                case VirtualKey.Number0: return RuntimeKey.RightParen; // ')'
                default: break;
            }
        }

        // Fall back to VirtualKey mapping (covers letters, keypad, arrows, function keys, etc.)
        return key switch
        {
            // Alphabetic keys (A-Z)
            VirtualKey.A => RuntimeKey.A,
            VirtualKey.B => RuntimeKey.B,
            VirtualKey.C => RuntimeKey.C,
            VirtualKey.D => RuntimeKey.D,
            VirtualKey.E => RuntimeKey.E,
            VirtualKey.F => RuntimeKey.F,
            VirtualKey.G => RuntimeKey.G,
            VirtualKey.H => RuntimeKey.H,
            VirtualKey.I => RuntimeKey.I,
            VirtualKey.J => RuntimeKey.J,
            VirtualKey.K => RuntimeKey.K,
            VirtualKey.L => RuntimeKey.L,
            VirtualKey.M => RuntimeKey.M,
            VirtualKey.N => RuntimeKey.N,
            VirtualKey.O => RuntimeKey.O,
            VirtualKey.P => RuntimeKey.P,
            VirtualKey.Q => RuntimeKey.Q,
            VirtualKey.R => RuntimeKey.R,
            VirtualKey.S => RuntimeKey.S,
            VirtualKey.T => RuntimeKey.T,
            VirtualKey.U => RuntimeKey.U,
            VirtualKey.V => RuntimeKey.V,
            VirtualKey.W => RuntimeKey.W,
            VirtualKey.X => RuntimeKey.X,
            VirtualKey.Y => RuntimeKey.Y,
            VirtualKey.Z => RuntimeKey.Z,

            // Number keys (0-9) - main keyboard
            VirtualKey.Number0 => RuntimeKey.Alpha0,
            VirtualKey.Number1 => RuntimeKey.Alpha1,
            VirtualKey.Number2 => RuntimeKey.Alpha2,
            VirtualKey.Number3 => RuntimeKey.Alpha3,
            VirtualKey.Number4 => RuntimeKey.Alpha4,
            VirtualKey.Number5 => RuntimeKey.Alpha5,
            VirtualKey.Number6 => RuntimeKey.Alpha6,
            VirtualKey.Number7 => RuntimeKey.Alpha7,
            VirtualKey.Number8 => RuntimeKey.Alpha8,
            VirtualKey.Number9 => RuntimeKey.Alpha9,

            // Function keys (F1-F15)
            VirtualKey.F1 => RuntimeKey.F1,
            VirtualKey.F2 => RuntimeKey.F2,
            VirtualKey.F3 => RuntimeKey.F3,
            VirtualKey.F4 => RuntimeKey.F4,
            VirtualKey.F5 => RuntimeKey.F5,
            VirtualKey.F6 => RuntimeKey.F6,
            VirtualKey.F7 => RuntimeKey.F7,
            VirtualKey.F8 => RuntimeKey.F8,
            VirtualKey.F9 => RuntimeKey.F9,
            VirtualKey.F10 => RuntimeKey.F10,
            VirtualKey.F11 => RuntimeKey.F11,
            VirtualKey.F12 => RuntimeKey.F12,
            VirtualKey.F13 => RuntimeKey.F13,
            VirtualKey.F14 => RuntimeKey.F14,
            VirtualKey.F15 => RuntimeKey.F15,

            // Numeric keypad (0-9)
            VirtualKey.NumberPad0 => RuntimeKey.Keypad0,
            VirtualKey.NumberPad1 => RuntimeKey.Keypad1,
            VirtualKey.NumberPad2 => RuntimeKey.Keypad2,
            VirtualKey.NumberPad3 => RuntimeKey.Keypad3,
            VirtualKey.NumberPad4 => RuntimeKey.Keypad4,
            VirtualKey.NumberPad5 => RuntimeKey.Keypad5,
            VirtualKey.NumberPad6 => RuntimeKey.Keypad6,
            VirtualKey.NumberPad7 => RuntimeKey.Keypad7,
            VirtualKey.NumberPad8 => RuntimeKey.Keypad8,
            VirtualKey.NumberPad9 => RuntimeKey.Keypad9,

            // Keypad operators
            VirtualKey.Multiply => RuntimeKey.KeypadMultiply,
            VirtualKey.Add => RuntimeKey.KeypadPlus,
            VirtualKey.Subtract => RuntimeKey.KeypadMinus,
            VirtualKey.Decimal => RuntimeKey.KeypadPeriod,
            VirtualKey.Divide => RuntimeKey.KeypadDivide,

            // Arrow keys
            VirtualKey.Up => RuntimeKey.UpArrow,
            VirtualKey.Down => RuntimeKey.DownArrow,
            VirtualKey.Left => RuntimeKey.LeftArrow,
            VirtualKey.Right => RuntimeKey.RightArrow,

            // Navigation keys
            VirtualKey.Home => RuntimeKey.Home,
            VirtualKey.End => RuntimeKey.End,
            VirtualKey.PageUp => RuntimeKey.PageUp,
            VirtualKey.PageDown => RuntimeKey.PageDown,
            VirtualKey.Insert => RuntimeKey.Insert,
            VirtualKey.Delete => RuntimeKey.Delete,

            // Modifier keys
            VirtualKey.Shift => RuntimeKey.LeftShift,
            VirtualKey.Control => RuntimeKey.LeftControl,
            VirtualKey.Menu => RuntimeKey.LeftAlt,
            VirtualKey.LeftShift => RuntimeKey.LeftShift,
            VirtualKey.RightShift => RuntimeKey.RightShift,
            VirtualKey.LeftControl => RuntimeKey.LeftControl,
            VirtualKey.RightControl => RuntimeKey.RightControl,
            VirtualKey.LeftMenu => RuntimeKey.LeftAlt,
            VirtualKey.RightMenu => RuntimeKey.RightAlt,
            VirtualKey.LeftWindows => RuntimeKey.LeftMeta,
            VirtualKey.RightWindows => RuntimeKey.RightMeta,

            // Lock keys
            VirtualKey.CapitalLock => RuntimeKey.CapsLock,
            VirtualKey.NumberKeyLock => RuntimeKey.NumLock,
            VirtualKey.Scroll => RuntimeKey.ScrollLock,

            // Control keys
            VirtualKey.Space => RuntimeKey.Space,
            VirtualKey.Enter => RuntimeKey.Return,
            VirtualKey.Tab => RuntimeKey.Tab,
            VirtualKey.Back => RuntimeKey.BackSpace,
            VirtualKey.Escape => RuntimeKey.Escape,

            // Special keys
            VirtualKey.Pause => RuntimeKey.Pause,
            VirtualKey.Clear => RuntimeKey.Clear,
            VirtualKey.Print => RuntimeKey.Print,
            VirtualKey.Help => RuntimeKey.Help,
            VirtualKey.Application => RuntimeKey.Menu,

            // Unsupported keys
            _ => RuntimeKey.None,
        };
    }

    /// <summary>
    /// Determines which mouse button is pressed from pointer properties.
    /// </summary>
    /// <param name="properties">The pointer point properties from a WinUI pointer event.</param>
    /// <returns>The corresponding engine RuntimeMouseButton value.</returns>
    public static RuntimeMouseButton TranslateMouseButton(PointerPointProperties properties)
        => properties switch
        {
            { IsLeftButtonPressed: true } => RuntimeMouseButton.Left,
            { IsRightButtonPressed: true } => RuntimeMouseButton.Right,
            { IsMiddleButtonPressed: true } => RuntimeMouseButton.Middle,
            { IsXButton1Pressed: true } => RuntimeMouseButton.ExtButton1,
            { IsXButton2Pressed: true } => RuntimeMouseButton.ExtButton2,
            _ => RuntimeMouseButton.None,
        };
}
