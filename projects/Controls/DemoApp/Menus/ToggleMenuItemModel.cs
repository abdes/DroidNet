// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
///     View model for a toggle hosted inside a menu item template.
/// </summary>
public sealed partial class ToggleMenuItemModel : ObservableObject
{
    /// <summary>
    ///     Gets the content shown when the toggle is off.
    /// </summary>
    public string OffContent { get; init; } = "Off";

    /// <summary>
    ///     Gets the content shown when the toggle is on.
    /// </summary>
    public string OnContent { get; init; } = "On";

    /// <summary>
    ///     Gets the minimum toggle width.
    /// </summary>
    public double MinWidth { get; init; } = 92;

    [ObservableProperty]
    public partial bool IsOn { get; set; }
}
