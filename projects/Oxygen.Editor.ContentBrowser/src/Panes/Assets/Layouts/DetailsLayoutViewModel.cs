// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// The ViewModel for the <see cref="DetailsLayoutView"/> view: a table with sortable columns.
/// </summary>
/// <param name="assetProvider">The shared content-browser asset provider.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="contentBrowserState">The content-browser state.</param>
/// <param name="hostingContext">The hosting context.</param>
/// <param name="scripts">The script analysis, which shows script problems in each row's status.</param>
public partial class DetailsLayoutViewModel(
    IAssetFeed assetProvider,
    IProjectContextService projectContextService,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext,
    Scripts.IScriptAnalysisService? scripts = null)
    : AssetsLayoutViewModel(assetProvider, projectContextService, contentBrowserState, hostingContext, scripts)
{
    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The content browser asset row to invoke.</param>
    [RelayCommand]
    private void InvokeItem(AssetEntry item) => this.OnItemInvoked(item);
}
