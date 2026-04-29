// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.Menus;

/// <summary>
///     View model for a slider hosted inside a menu item template.
/// </summary>
public sealed partial class SliderMenuItemModel : ObservableObject
{
    /// <summary>
    ///     Gets the minimum slider value.
    /// </summary>
    public double Minimum { get; init; }

    /// <summary>
    ///     Gets the maximum slider value.
    /// </summary>
    public double Maximum { get; init; }

    /// <summary>
    ///     Gets the slider step frequency.
    /// </summary>
    public double StepFrequency { get; init; }

    /// <summary>
    ///     Gets the preferred editor width.
    /// </summary>
    public double Width { get; init; } = 140;

    [ObservableProperty]
    public partial double Value { get; set; }
}
