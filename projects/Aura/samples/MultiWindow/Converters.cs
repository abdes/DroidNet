// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Samples.Aura.MultiWindow.Converters;

/// <summary>
/// Converts a boolean value to a background brush (accent for true, transparent for false).
/// Used to highlight active windows in the window list.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed class BoolToBackgroundConverter : IValueConverter
{
    /// <inheritdoc/>
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        _ = targetType;
        _ = parameter;
        _ = language;

        if (value is bool isActive && isActive)
        {
            return Application.Current.Resources["SubtleFillColorSecondaryBrush"] as Brush;
        }

        return new SolidColorBrush(Microsoft.UI.Colors.Transparent);
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

/// <summary>
/// Converts zero or negative numbers to Visible, positive numbers to Collapsed.
/// Used to show empty state when no windows are open.
/// </summary>
[ExcludeFromCodeCoverage]
public sealed class ZeroToVisibilityConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        _ = targetType;
        _ = parameter;
        _ = language;

        if (value is int intValue)
        {
            return intValue <= 0 ? Visibility.Visible : Visibility.Collapsed;
        }

        return Visibility.Collapsed;
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
