// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>Shows assets in the window's Content Browser and brings it forward.</summary>
/// <param name="placement">Where panels show.</param>
public sealed class ContentBrowserAssetReveal(IPlacementService placement) : IAssetReveal
{
    /// <summary>The Content Browser panel's identity.</summary>
    public const string PanelId = "cb";

    private ContentBrowserViewModel? browser;

    /// <inheritdoc />
    public async Task<bool> RevealAsync(IReadOnlyList<AssetPath> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        if (assets.Count == 0 || this.browser is not { } current
            || !await (assets is [var single] ? current.ShowAssetAsync(single) : current.ShowAssetsAsync(assets)).ConfigureAwait(true))
        {
            return false;
        }

        _ = await placement.ShowPanelAsync(PanelId).ConfigureAwait(true);
        return true;
    }

    /// <summary>Makes a Content Browser the one that shows assets.</summary>
    /// <param name="model">The browser, which the window's router created.</param>
    internal void Attach(ContentBrowserViewModel model) => this.browser = model;

    /// <summary>Forgets a Content Browser that ended.</summary>
    /// <param name="model">The browser.</param>
    internal void Detach(ContentBrowserViewModel model)
    {
        if (ReferenceEquals(this.browser, model))
        {
            this.browser = null;
        }
    }
}
