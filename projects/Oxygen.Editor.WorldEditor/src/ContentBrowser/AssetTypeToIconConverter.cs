// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
///     Converts an <see cref="AssetType" /> to a corresponding icon glyph.
/// </summary>
public partial class AssetTypeToIconConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is AssetType assetType)
        {
            return assetType switch
            {
                AssetType.Image => "\uE8B9", // image
                AssetType.Scene => "\uE914", // scene
                AssetType.Mesh => "\uE8C1", // mesh
                AssetType.Folder => "\uE8B7", // folder (filled)
                AssetType.Unknown => "\uE8A5", // unknown
                _ => "\uE8A5",
            };
        }

        return "\uE8A5"; // Example glyph for unknown
    }

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
