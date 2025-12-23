// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="ListLayoutView"/> view.
/// </summary>
/// <param name="assetsIndexingService">The service responsible for indexing assets.</param>
public partial class ListLayoutViewModel(IAssetIndexingService assetsIndexingService, ContentBrowserState contentBrowserState, HostingContext hostingContext) : AssetsLayoutViewModel(assetsIndexingService, contentBrowserState, hostingContext)
{
    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The game asset to invoke.</param>
    [RelayCommand]
    private void InvokeItem(GameAsset item) => this.OnItemInvoked(item);
}
