// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>Shows assets in the window's Content Explorer and brings it forward.</summary>
/// <param name="placement">Where panels show.</param>
public sealed class ContentExplorerAssetReveal(IPlacementService placement) : IAssetReveal
{
    private ContentExplorerViewModel? explorer;

    /// <inheritdoc />
    public async Task<bool> RevealAsync(IReadOnlyList<AssetPath> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        if (assets.Count == 0 || this.explorer is not { } current
            || !await (assets is [var single] ? current.ShowAssetAsync(single) : current.ShowAssetsAsync(assets)).ConfigureAwait(true))
        {
            return false;
        }

        _ = await placement.ShowPanelAsync(ContentExplorerModule.PanelId).ConfigureAwait(true);
        return true;
    }

    /// <summary>Makes a Content Explorer the one that shows assets.</summary>
    /// <param name="model">The explorer, which the window's router created.</param>
    internal void Attach(ContentExplorerViewModel model) => this.explorer = model;

    /// <summary>Forgets a Content Explorer that ended.</summary>
    /// <param name="model">The explorer.</param>
    internal void Detach(ContentExplorerViewModel model)
    {
        if (ReferenceEquals(this.explorer, model))
        {
            this.explorer = null;
        }
    }
}
