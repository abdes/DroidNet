// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Specifies the position of a label relative to a toolbar control.
/// </summary>
public enum ToolBarLabelPosition
{
    /// <summary>
    /// Automatically inherits from parent ToolBar's DefaultLabelPosition.
    /// </summary>
    Auto = 0,

    /// <summary>
    /// The label is not displayed.
    /// </summary>
    Collapsed = 1,

    /// <summary>
    /// The label is positioned to the right of the icon.
    /// </summary>
    Right = 2,

    /// <summary>
    /// The label is positioned below the icon.
    /// </summary>
    Bottom = 3,

    /// <summary>
    /// The label is positioned to the left of the icon.
    /// </summary>
    Left = 4,

    /// <summary>
    /// The label is positioned above the icon.
    /// </summary>
    Top = 5,
}
