// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Converters used by the OutputConsole control for data binding.
/// </summary>
internal sealed partial class BoolToVisibilityConverter : IValueConverter
{
    /// <summary>
    ///     Converts a boolean value to a <see cref="Visibility" /> value.
    ///     Returns <see cref="Visibility.Visible" /> when the input is <see langword="true" />,
    ///     otherwise <see cref="Visibility.Collapsed" />.
    /// </summary>
    /// <param name="value">The source data being passed to the target. Expected to be a <see cref="bool" />.</param>
    /// <param name="targetType">The type of the binding target property. (Ignored.)</param>
    /// <param name="parameter">An optional parameter to be used in the converter. (Ignored.)</param>
    /// <param name="language">The culture to respect during conversion. (Ignored.)</param>
    /// <returns>
    ///     <see cref="Visibility.Visible" /> if <paramref name="value" /> is <see langword="true" />; otherwise
    ///     <see cref="Visibility.Collapsed" />.
    /// </returns>
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is true ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>
    ///     Converts back from a <see cref="Visibility" /> value to a <see cref="bool" />.
    ///     Returns <see langword="true" /> if the input is <see cref="Visibility.Visible" />, otherwise
    ///     <see langword="false" />.
    /// </summary>
    /// <param name="value">The value produced by the binding target. Expected to be a <see cref="Visibility" />.</param>
    /// <param name="targetType">The type to convert to. (Ignored.)</param>
    /// <param name="parameter">An optional parameter to be used in the converter. (Ignored.)</param>
    /// <param name="language">The culture to respect during conversion. (Ignored.)</param>
    /// <returns>
    ///     <see langword="true" /> when <paramref name="value" /> is <see cref="Visibility.Visible" />, otherwise
    ///     <see langword="false" />.
    /// </returns>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => value is Visibility.Visible;
}
