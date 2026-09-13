// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>Handles explicit navigation to asset identities without changing cook state.</summary>
public sealed partial class ContentBrowserViewModel
{
    /// <summary>Shows a source/dependency identity in its folder, clearing filters only when they would hide it.</summary>
    /// <param name="assetUri">The asset to reveal.</param>
    /// <returns>Whether the requested identity became the current browser selection.</returns>
    public async Task<bool> ShowAssetAsync(Uri assetUri)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        if (this.isDisposed || this.childContainer is null || this.localRouter is null || this.browserState is null)
        {
            return false;
        }

        var project = projectContextService.ActiveProject;
        var item = await this.childContainer.Resolve<IContentBrowserAssetProvider>().ResolveAsync(assetUri).ConfigureAwait(true);
        if (item is null || item.PrimaryState == AssetState.Missing || !ReferenceEquals(project, projectContextService.ActiveProject) || this.isDisposed)
        {
            return false;
        }

        var path = AssetUriHelper.GetVirtualPath(item.IdentityUri);
        var folder = path[..path.LastIndexOf('/')];
        if (item.SourcePath is null && item.DescriptorPath is null && !item.IsBuiltin)
        {
            folder = "/Cooked" + folder;
        }

        if (!this.Query.Matches(item))
        {
            this.Query.ClearAllCommand.Execute(parameter: null);
        }

        await this.localRouter.NavigateAsync("/(left:project//right:" + this.currentAssetsViewPath + ")" + RouteStateMapping.BuildSelectedQuery([folder])).ConfigureAwait(true);
        if (this.browserState.ActiveAssetLayout is not { } layout || !ReferenceEquals(project, projectContextService.ActiveProject))
        {
            return false;
        }

        layout.SelectedAsset = item;
        layout.RevealSelection();
        return layout.SelectedAsset is { } selected && AssetIdentityGrouping.Represents(selected, item.IdentityUri);
    }
}
