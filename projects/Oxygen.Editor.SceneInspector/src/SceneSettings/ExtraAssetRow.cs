// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Displays an additional asset with its current edit availability.</summary>
/// <param name="Asset">The asset.</param>
/// <param name="CanEdit">Whether the row can be removed.</param>
public sealed record ExtraAssetRow(AssetPath Asset, bool CanEdit)
{
    /// <summary>Gets the asset's virtual path, as the row shows it.</summary>
    public string Path => this.Asset.VirtualPath;
}
