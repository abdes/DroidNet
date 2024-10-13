// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Converters;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

/// <summary>Convert a <see langword="null" /> value to a <see cref="Visibility" /> value.</summary>
public partial class NullToVisibilityConverter : IValueConverter
{
    /// <summary>
    /// Convert a <see langword="null" /> value to a <see cref="Visibility" /> value. When the input value is <see langword="null" />, the <paramref name="parameter" /> can be used to specify the <see cref="Visibility" /> value to return.
    /// </summary>
    /// <param name="value">The value (that can be null) to be converted.</param>
    /// <param name="targetType">The type of the binding target property. Should always be <c>typeof(Visibility)</c>.</param>
    /// <param name="parameter">
    /// May contain the <see cref="Visibility" /> value to return if <paramref name="value" /> is <see langword="null" />.
    /// </param>
    /// <param name="language">The culture to use in the converter. Not used for this conversion.</param>
    /// <returns>
    /// A <see cref="Visibility" /> value that is either <see cref="Visibility.Visible" /> if the <paramref name="value" /> is not
    /// null, or the <see cref="Visibility" /> enum value corresponding to the string passed in <paramref name="parameter" /> if
    /// there is one, or <see cref="Visibility.Collapsed" />.
    /// </returns>
    public object Convert(object? value, Type targetType, object? parameter, string language)
    {
        var invisibility = Visibility.Collapsed;

        if (parameter is string preferredInvisibility && Enum.TryParse(
                typeof(Visibility),
                preferredInvisibility,
                ignoreCase: true,
                out var result))
        {
            invisibility = (Visibility)result;
        }

        return value == null ? invisibility : Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException("Don't use NullToVisibilityConverter.ConvertBack; it's meaningless.");
}
