// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>
/// Defines the layout configuration for the scene editor viewports.
/// </summary>
public enum SceneViewLayout
{
    /// <summary>A single viewport filling the entire area.</summary>
    OnePane,

    /// <summary>Two viewports arranged vertically (side-by-side), main on the left.</summary>
    TwoMainLeft,

    /// <summary>Two viewports arranged vertically (side-by-side), main on the right.</summary>
    TwoMainRight,

    /// <summary>Two viewports arranged horizontally (stacked), main on the top.</summary>
    TwoMainTop,

    /// <summary>Two viewports arranged horizontally (stacked), main on the bottom.</summary>
    TwoMainBottom,

    /// <summary>Three panes where the main occupies half the area on the left and the remaining two share the right half vertically.</summary>
    ThreeMainLeft,

    /// <summary>Three panes where the main occupies half the area on the right and the remaining two share the left half vertically.</summary>
    ThreeMainRight,

    /// <summary>Three panes where the main occupies the top half and the remaining two share the bottom half horizontally.</summary>
    ThreeMainTop,

    /// <summary>Three panes where the main occupies the bottom half and the remaining two share the top half horizontally.</summary>
    ThreeMainBottom,

    /// <summary>Four panes where the main occupies two thirds on the left and the remaining three share the right third stacked.</summary>
    FourMainLeft,

    /// <summary>Four panes where the main occupies two thirds on the right and the remaining three share the left third stacked.</summary>
    FourMainRight,

    /// <summary>Four panes where the main occupies two thirds at the top and the remaining three share the bottom third across.</summary>
    FourMainTop,

    /// <summary>Four panes where the main occupies two thirds at the bottom and the remaining three share the top third across.</summary>
    FourMainBottom,

    /// <summary>Four viewports arranged in a 2x2 grid.</summary>
    FourQuad,
}
