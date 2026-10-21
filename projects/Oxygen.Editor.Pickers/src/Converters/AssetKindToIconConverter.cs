// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Assets;

namespace Oxygen.Editor.Pickers.Converters;

/// <summary>
///     Converts an <see cref="AssetKind" /> to a corresponding icon glyph.
/// </summary>
public partial class AssetKindToIconConverter : IValueConverter
{
    /// <inheritdoc />
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is AssetKind assetKind)
        {
            return assetKind switch
            {
                AssetKind.Image => "\uE8B9",
                AssetKind.Scene => "\uE914",
                AssetKind.Script => "\uE943",
                AssetKind.InputAction => "\uE765",
                AssetKind.InputMappingContext => "\uE721",
                AssetKind.InputFile => "\uE943",
                AssetKind.PhysicsScene => "\uE9D9",
                AssetKind.Geometry => "\uF158",
                AssetKind.Material => "\uE790",
                AssetKind.Texture => "\uE8B9",
                AssetKind.Folder => "\uE8B7",
                _ => "\uE8A5",
            };
        }

        return "\uE8A5"; // Example glyph for unknown
    }

    /// <inheritdoc />
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new InvalidOperationException();
}
