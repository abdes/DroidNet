// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Samples.Aura.MultiWindow.Converters;

/// <summary>
/// Converts zero or negative numbers to Visible, positive numbers to Collapsed.
/// Used to show empty state when no windows are open.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class ZeroToVisibilityConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        _ = targetType;
        _ = parameter;
        _ = language;

        return value is int intValue ? intValue <= 0
            ? Visibility.Visible
            : Visibility.Collapsed : Visibility.Collapsed;
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
    {
        _ = value;
        _ = targetType;
        _ = parameter;
        _ = language;
        throw new NotSupportedException();
    }
}
