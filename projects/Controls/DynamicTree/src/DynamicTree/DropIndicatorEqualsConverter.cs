// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Controls;

/// <summary>
/// Converts a <see cref="DynamicTree.DropIndicatorPosition"/> value to a <see cref="Visibility"/> by comparing it to a parameter.
/// </summary>
public sealed class DropIndicatorEqualsConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is DynamicTree.DropIndicatorPosition position && parameter is not null)
        {
            var parameterValue = parameter.ToString();
            if (!string.IsNullOrEmpty(parameterValue)
                && Enum.TryParse(parameterValue, ignoreCase: true, out DynamicTree.DropIndicatorPosition expected))
            {
                return position == expected ? Visibility.Visible : Visibility.Collapsed;
            }
        }

        return Visibility.Collapsed;
    }

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}
