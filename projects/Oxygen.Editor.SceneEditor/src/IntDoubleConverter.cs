// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Binds an integer view-model value to a NumberBox, whose value is a double. Converts the integer to a double, and
/// the edited double back to the nearest integer.
/// </summary>
public sealed partial class IntDoubleConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
        => value switch
        {
            int number => (double)number,
            long number => (double)number,
            double number => number,
            float number => (double)number,
            _ => 0d,
        };

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value switch
        {
            double number => (int)Math.Round(number),
            float number => (int)Math.Round(number),
            string text when int.TryParse(text, NumberStyles.Integer, CultureInfo.CurrentCulture, out var parsed) => parsed,
            _ => 0,
        };
}
