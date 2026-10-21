// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;

/// <summary>
/// The ViewModel for the <see cref="ListLayoutView"/> view.
/// </summary>
/// <param name="assetProvider">The shared content-explorer asset provider.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="contentExplorerState">The content-explorer state.</param>
/// <param name="hostingContext">The hosting context.</param>
/// <param name="scripts">The scripts, whose problems show in each row's status.</param>
public partial class ListLayoutViewModel(
    IAssetFeed assetProvider,
    IProjectContextService projectContextService,
    ContentExplorerState contentExplorerState,
    HostingContext hostingContext,
    Oxygen.Editor.Scripting.IScripts? scripts = null)
    : AssetsLayoutViewModel(assetProvider, projectContextService, contentExplorerState, hostingContext, scripts)
{
    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The content explorer asset row to invoke.</param>
    [RelayCommand]
    private void InvokeItem(AssetEntry item) => this.OnItemInvoked(item);
}
