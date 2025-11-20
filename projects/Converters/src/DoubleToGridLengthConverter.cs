// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Converters;

/// <summary>
///     Converts a double (pixels) to a GridLength and back; supports two-way binding.
/// </summary>
public sealed partial class DoubleToGridLengthConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is double d
            ? new GridLength(d, GridUnitType.Pixel)
            : new GridLength(0, GridUnitType.Pixel);

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value is GridLength gl && gl.IsAbsolute
            ? gl.Value
            : 0.0;
}
