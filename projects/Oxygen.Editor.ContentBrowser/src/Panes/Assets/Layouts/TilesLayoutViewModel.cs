// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.ContentBrowser.Models;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// The ViewModel for the tiles layout view.
/// </summary>
/// <param name="assetCatalog">The asset catalog.</param>
public partial class TilesLayoutViewModel(
    IAssetCatalog assetCatalog,
    IProject currentProject,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext)
    : AssetsLayoutViewModel(assetCatalog, currentProject, contentBrowserState, hostingContext)
{
    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The game asset to invoke.</param>
    [RelayCommand]
    private void InvokeItem(GameAsset item) => this.OnItemInvoked(item);
}
