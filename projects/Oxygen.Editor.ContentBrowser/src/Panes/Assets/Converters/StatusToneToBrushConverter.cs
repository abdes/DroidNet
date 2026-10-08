// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Converters;

/// <summary>Converts a status tone (Neutral, Caution, Critical or Success) to the theme's status dot brush.</summary>
public sealed partial class StatusToneToBrushConverter : IValueConverter
{
    /// <inheritdoc />
    public object? Convert(object value, Type targetType, object parameter, string language)
    {
        var key = (value as string) switch
        {
            "Success" => "SystemFillColorSuccessBrush",
            "Caution" => "SystemFillColorCautionBrush",
            "Critical" => "SystemFillColorCriticalBrush",
            _ => "TextFillColorTertiaryBrush",
        };
        return Application.Current?.Resources.TryGetValue(key, out var brush) == true ? brush as Brush : null;
    }

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
