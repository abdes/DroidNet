// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspection;

/// <summary>An explicit navigation target in an inspection report.</summary>
/// <param name="Asset">The retained logical identity.</param>
public sealed record InspectionAssetLink(AssetPath Asset)
{
    /// <summary>Gets the asset name.</summary>
    public string Name => DisplayName(this.Asset);

    /// <summary>Gets the complete reference for selection/copy and tooltips.</summary>
    public string Path => this.Asset.VirtualPath;

    /// <summary>Formats a source or cooked filename while preserving engine generator names.</summary>
    /// <param name="asset">The asset identity.</param>
    /// <returns>The readable asset name.</returns>
    internal static string DisplayName(AssetPath asset)
    {
        var name = asset.Name;
        foreach (var suffix in new[] { AssetFileTypes.MaterialDescriptor, AssetFileTypes.GeometryDescriptor, AssetFileTypes.SceneDescriptor, AssetFileTypes.Material, AssetFileTypes.Geometry, AssetFileTypes.Scene })
        {
            if (name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return name[..^suffix.Length];
            }
        }

        return name;
    }
}
