// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
///     View model for a number input hosted inside a menu item template.
/// </summary>
public sealed partial class NumberBoxMenuItemModel : ObservableObject
{
    /// <summary>
    ///     Gets the number formatting mask.
    /// </summary>
    public string Mask { get; init; } = "~.#";

    /// <summary>
    ///     Gets the multiplier used by the number input.
    /// </summary>
    public int Multiplier { get; init; } = 1;

    /// <summary>
    ///     Gets the preferred editor width.
    /// </summary>
    public double Width { get; init; } = 96;

    [ObservableProperty]
    public partial float NumberValue { get; set; }
}
