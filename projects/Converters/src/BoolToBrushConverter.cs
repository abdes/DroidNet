// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Converters;

/// <summary>
/// Select a color <see cref="Brush" /> based on a <see langword="bool" /> value.
/// </summary>
public partial class BoolToBrushConverter : IValueConverter
{
    /// <summary>
    /// Gets or sets the <see cref="Brush" /> to use when the value is <see langword="true" />.
    /// </summary>
    public Brush ActiveBrush { get; set; } = new SolidColorBrush(Colors.Red);

    /// <summary>
    /// Gets or sets the <see cref="Brush" /> to use when the value is <see langword="false" />.
    /// </summary>
    public Brush InactiveBrush { get; set; } = new SolidColorBrush(Colors.Transparent);

    /// <summary>
    /// Convert a <see langword="bool" /> value to a <see cref="Brush" /> value. When the input value is <see langword="true" />,
    /// the <see cref="ActiveBrush" /> is used; otherwise the <see cref="InactiveBrush" /> is used.
    /// </summary>
    /// <param name="value">The value to be converted.</param>
    /// <param name="targetType">The type of the binding target property. Should always be <c>typeof(<see cref="Brush" />)</c>.</param>
    /// <param name="parameter">unused.</param>
    /// <param name="language">Unused.</param>
    /// <returns>
    /// A <see cref="Brush" /> value that is either <see cref="ActiveBrush" /> if the <paramref name="value" /> is <see langword="true" />,
    /// or the <see cref="InactiveBrush" /> otherwise.
    /// </returns>
    /// <exception cref="InvalidOperationException">
    /// If the <paramref name="value" /> to convert is not a boolean or the <paramref name="targetType">target type</paramref> is
    /// not <see cref="Brush" />.
    /// </exception>
    public object Convert(object value, Type targetType, object? parameter, string language)
    {
        _ = parameter; // unused
        _ = language; // unused

        return value is bool isActive && targetType == typeof(Brush)
            ? (object)(isActive ? this.ActiveBrush : this.InactiveBrush)
            : throw new InvalidOperationException("Invalid types. Expected: (bool, Brush).");
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
