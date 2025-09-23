// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
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
            switch (assetType)
            {
                case AssetType.Folder:
                    // Use a warm yellow; prefer theme resource if available
                    return new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0xE6, 0xB8, 0x00)); // amber-ish
                default:
                    // Default foreground (let the control theme decide) - use white
                    return new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0xFF, 0xFF, 0xFF));
            }
        }

        return new SolidColorBrush(ColorHelper.FromArgb(0xFF, 0x6B, 0x6B, 0x6B));
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
