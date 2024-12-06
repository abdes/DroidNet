// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Converts an <see cref="AssetType"/> to a corresponding icon glyph.
/// </summary>
public partial class AssetTypeToIconConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is AssetType assetType)
        {
            return assetType switch
            {
                AssetType.Image => "\uE8B9", // Example glyph for image
                AssetType.Scene => "\uE914", // Example glyph for scene
                AssetType.Mesh => "\uE8C1", // Example glyph for mesh
                AssetType.Unknown => "\uE8A5", // Example glyph for unknown
                _ => "\uE8A5", // Example glyph for unknown
            };
        }

        return "\uE8A5"; // Example glyph for unknown
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
