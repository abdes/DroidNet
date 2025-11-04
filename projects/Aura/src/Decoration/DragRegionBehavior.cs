// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Specifies the behavior of the title bar drag region for window movement.
/// </summary>
/// <remarks>
///     The drag region determines which areas of the title bar can be used to drag and move the
///     window. Different behaviors support various UI patterns, from standard desktop windows to
///     custom interactive title bars.
/// </remarks>
public enum DragRegionBehavior
{
    /// <summary>
    ///     Standard drag region behavior. The entire title bar area can be used for window
    ///     dragging, except for interactive elements like buttons and menus.
    /// </summary>
    Default,

    /// <summary>
    ///     Extended drag region that includes additional areas beyond the standard title bar.
    ///     Useful for windows with custom chrome that need larger drag areas.
    /// </summary>
    Extended,

    /// <summary>
    ///     Custom drag region defined by the application. The window developer is responsible for
    ///     specifying exact drag regions via platform-specific APIs.
    /// </summary>
    Custom,

    /// <summary>
    ///     No drag region. The title bar cannot be used for window dragging. Useful for windows
    ///     requiring custom interaction patterns or fixed positioning.
    /// </summary>
    None,
}
