// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Pickers;

/// <summary>One choice an asset picker shows.</summary>
/// <param name="Name">The name the row shows.</param>
/// <param name="Asset">The asset the choice assigns; <see langword="null"/> for the choice that clears the assignment.</param>
/// <param name="DisplayType">The type and status line under the name.</param>
/// <param name="DisplayPath">The location the row's tooltip shows.</param>
/// <param name="IsEnabled">Whether the choice can be picked.</param>
/// <param name="Glyph">The icon glyph of the row's thumbnail.</param>
public sealed record AssetChoice(string Name, AssetPath? Asset, string DisplayType, string DisplayPath, bool IsEnabled, string Glyph)
{
    /// <summary>Describes an accepted asset: its name, its type and status, its location, and whether it can be picked.</summary>
    /// <param name="asset">The asset.</param>
    /// <param name="type">The asset type shown before its status, such as <c>Texture</c>.</param>
    /// <param name="glyph">The icon glyph of the row's thumbnail.</param>
    /// <returns>The choice that assigns the asset.</returns>
    public static AssetChoice For(AssetEntry asset, string type, string glyph)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return new(asset.DisplayName, asset.Identity, type + " · " + AssetStatusText.ForEntry(asset), asset.DisplayPath, asset.IsSelectable, glyph);
    }
}
