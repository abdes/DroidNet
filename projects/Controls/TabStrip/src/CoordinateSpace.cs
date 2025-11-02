// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Defines the coordinate space in which a point is expressed.
/// </summary>
public enum CoordinateSpace
{
    /// <summary>
    ///     Coordinates relative to a specific UIElement (logical DIPs).
    /// </summary>
    Element,

    /// <summary>
    ///     Coordinates relative to the top-level window (logical DIPs).
    /// </summary>
    Window,

    /// <summary>
    ///     Physical screen coordinates (DPI-aware, raw pixels).
    /// </summary>
    Screen,
}
