// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
///     Converts an <see cref="AssetType" /> to a Brush for icon foregrounds.
/// </summary>
public sealed class AssetTypeToBrushConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is AssetType assetType)
        {
            // Distinct colors per asset type, inspired by editor UIs
            return assetType switch
            {
                AssetType.Image => new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x3C, 0x9D, 0xD0)), // teal/blue
                AssetType.Scene => new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0xF7, 0xB5, 0x00)), // orange/yellow
                AssetType.Mesh => new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x86, 0xC0, 0x44)), // green
                AssetType.Folder => TryGetResourceBrush("FolderAssetBrush") ??
                                    new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0xE6, 0xB8,
                                        0x00)), // theme-aware if resource exists
                AssetType.Unknown => new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x9E, 0x9E, 0x9E)), // gray
                _ => new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x9E, 0x9E, 0x9E)),
            };
        }

        // Fallback color
        return new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x9E, 0x9E, 0x9E));
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();

    // Local helper
    private static SolidColorBrush? TryGetResourceBrush(string key)
    {
        try
        {
            // Try Application-level resources first
            if (Application.Current?.Resources.TryGetValue(key, out var v) == true
                && v is SolidColorBrush b)
            {
                return b;
            }
        }
        catch
        {
            // ignore; fall back occurs at call site
        }

        return null;
    }
}
