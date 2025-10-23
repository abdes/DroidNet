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
internal sealed partial class BoolToBackgroundConverter : IValueConverter
{
    /// <inheritdoc/>
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        _ = targetType;
        _ = parameter;
        _ = language;

        return value is bool isActive && isActive
            ? Application.Current.Resources["SubtleFillColorSecondaryBrush"] as Brush
            : (object)new SolidColorBrush(Microsoft.UI.Colors.Transparent);
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
