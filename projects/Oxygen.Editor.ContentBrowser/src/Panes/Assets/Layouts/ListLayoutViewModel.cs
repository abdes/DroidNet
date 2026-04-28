// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// The ViewModel for the <see cref="ListLayoutView"/> view.
/// </summary>
/// <param name="assetProvider">The shared content-browser asset provider.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="contentBrowserState">The content-browser state.</param>
/// <param name="hostingContext">The hosting context.</param>
public partial class ListLayoutViewModel(
    IContentBrowserAssetProvider assetProvider,
    IProjectContextService projectContextService,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext)
    : AssetsLayoutViewModel(assetProvider, projectContextService, contentBrowserState, hostingContext)
{
    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The content browser asset row to invoke.</param>
    [RelayCommand]
    private void InvokeItem(ContentBrowserAssetItem item) => this.OnItemInvoked(item);
}
