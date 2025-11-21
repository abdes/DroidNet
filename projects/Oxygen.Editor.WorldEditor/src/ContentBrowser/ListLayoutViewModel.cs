// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="ListLayoutView"/> view.
/// </summary>
/// <param name="assetsIndexingService">The service responsible for indexing assets.</param>
public partial class ListLayoutViewModel(AssetsIndexingService assetsIndexingService) : AssetsLayoutViewModel
{
    /// <summary>
    /// Gets the collection of game assets.
    /// </summary>
    public ObservableCollection<GameAsset> Assets { get; } = assetsIndexingService.Assets;

    /// <summary>
    /// Invokes the item.
    /// </summary>
    /// <param name="item">The game asset to invoke.</param>
    [RelayCommand]
    private void InvokeItem(GameAsset item) => this.OnItemInvoked(item);
}
