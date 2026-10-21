// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// Converts a boolean value into one of two configured <see cref="Thickness"/> values.
/// </summary>
public sealed partial class BoolToThicknessConverter : IValueConverter
{
    /// <summary>
    /// Gets or sets the thickness returned when the bound value is true.
    /// </summary>
    public Thickness TrueThickness { get; set; } = new Thickness(2);

    /// <summary>
    /// Gets or sets the thickness returned when the bound value is false.
    /// </summary>
    public Thickness FalseThickness { get; set; } = new Thickness(0);

    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is bool b && b ? this.TrueThickness : this.FalseThickness;

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value is Thickness t && t == this.TrueThickness;
}
