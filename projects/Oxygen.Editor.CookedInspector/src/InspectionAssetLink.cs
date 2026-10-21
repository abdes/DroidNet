// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.CookedInspector;

/// <summary>An explicit navigation target in an inspection report.</summary>
/// <param name="Asset">The retained logical identity.</param>
public sealed record InspectionAssetLink(AssetPath Asset)
{
    /// <summary>Gets the asset name.</summary>
    public string Name => DisplayName(this.Asset);

    /// <summary>Gets the complete reference for selection/copy and tooltips.</summary>
    public string Path => this.Asset.VirtualPath;

    /// <summary>Formats a source or cooked file name without its type suffix.</summary>
    /// <param name="asset">The asset identity.</param>
    /// <returns>The readable asset name.</returns>
    internal static string DisplayName(AssetPath asset) => AssetFileTypes.RemoveSuffix(asset.Name);
}
