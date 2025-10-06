// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Describes the input origin for a menu interaction event routed into <see cref="MenuInteractionController"/>.
/// </summary>
public enum MenuInteractionInputSource
{
    /// <summary>
    /// Interaction originated from pointer (mouse, pen, touch).
    /// </summary>
    PointerInput,

    /// <summary>
    /// Interaction originated from the keyboard (arrow keys, Enter, Space).
    /// </summary>
    KeyboardInput,

    /// <summary>
    /// Interaction originated programmatically.
    /// </summary>
    Programmatic,
}
