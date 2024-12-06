// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Windows.Foundation;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="ListLayoutView"/> view.
/// </summary>
public partial class ListLayoutViewModel(AssetsIndexingService assetsIndexingService) : AssetsLayoutViewModel
{
    public ObservableCollection<GameAsset> Assets { get; } = assetsIndexingService.Assets;

    [RelayCommand]
    private void InvokeItem(GameAsset item) => this.OnItemInvoked(item);
}

public class AssetsLayoutViewModel : ObservableObject
{
    public event TypedEventHandler<AssetsLayoutViewModel, AssetsViewItemInvokedEventArgs>? ItemInvoked;

    protected void OnItemInvoked(GameAsset item) => this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(item));
}

public class AssetsViewItemInvokedEventArgs(GameAsset invokedItem)
{
    public GameAsset InvokedItem { get; } = invokedItem;
}
