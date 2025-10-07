// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents the active navigation mode that determined the current menu path.
/// </summary>
public enum MenuNavigationMode
{
    /// <summary>
    ///     Unspecified navigation mode.
    /// </summary>
    Programmatic = 0,

    /// <summary>
    ///     Menu navigation is using pointer (mouse, pen, touch) interactions.
    /// </summary>
    PointerInput,

    /// <summary>
    ///     Menu navigation is using keyboard interactions.
    /// </summary>
    KeyboardInput,
}
