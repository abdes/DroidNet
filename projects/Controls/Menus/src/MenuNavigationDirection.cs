// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents the menu navigation direction, usually corresponding to an input event. Interpretation
///     of the direction is context specific, and may result in moving the focus between items in a row or
///     column, opening submenus, closing them, switching between column and root, etc.
/// </summary>
public enum MenuNavigationDirection
{
    /// <summary>
    ///     Move Left.
    /// </summary>
    Left,

    /// <summary>
    ///     Move right.
    /// </summary>
    Right,

    /// <summary>
    ///     Move up.
    /// </summary>
    Up,

    /// <summary>
    ///     Move down.
    /// </summary>
    Down,
}
