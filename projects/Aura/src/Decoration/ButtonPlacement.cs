// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Specifies the horizontal placement of window control buttons (minimize, maximize, close)
/// within the title bar.
/// </summary>
/// <remarks>
/// Button placement follows platform conventions by default but can be customized for
/// specific application requirements or branding needs.
/// </remarks>
public enum ButtonPlacement
{
    /// <summary>
    /// Buttons are placed on the left side of the title bar (macOS style).
    /// Typically used for applications targeting macOS users or for consistency
    /// with cross-platform designs.
    /// </summary>
    Left,

    /// <summary>
    /// Buttons are placed on the right side of the title bar (Windows style).
    /// This is the standard placement for Windows applications.
    /// </summary>
    Right,

    /// <summary>
    /// Buttons are placed according to platform conventions. On Windows, this places
    /// buttons on the right; on macOS, on the left. Recommended for applications
    /// targeting multiple platforms.
    /// </summary>
    Auto,
}
