// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="AssetsView"/> view.
/// </summary>
/// <param name="currentProject">The current project.</param>
/// <param name="assetsIndexingService">The service responsible for indexing assets.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
public partial class AssetsViewModel(
    IProject currentProject,
    AssetsIndexingService assetsIndexingService,
    ViewModelToView vmToViewConverter) : AbstractOutletContainer, IRoutingAware
{
    /// <summary>
    /// Gets the layout view model.
    /// </summary>
    public object? LayoutViewModel => this.Outlets["right"].viewModel;

    /// <summary>
    /// Gets the converter for converting view models to views.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; } = vmToViewConverter;

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.Outlets.Add("right", (nameof(this.LayoutViewModel), null));

        this.PropertyChanging += this.OnLayoutViewModelChanging;
        this.PropertyChanged += this.OnLayoutViewModelChanged;

        await assetsIndexingService.IndexAssetsAsync().ConfigureAwait(true);
    }

    private void OnAssetItemInvoked(object? sender, AssetsViewItemInvokedEventArgs args)
    {
        _ = sender; // Unused

        if (args.InvokedItem.AssetType == AssetType.Scene)
        {
            // Update the scene explorer
            var scene = currentProject.Scenes.FirstOrDefault(scene => string.Equals(scene.Name, args.InvokedItem.Name, StringComparison.Ordinal));
            if (scene is not null)
            {
                currentProject.ActiveScene = scene;
            }
        }
    }

    private void OnLayoutViewModelChanging(object? sender, System.ComponentModel.PropertyChangingEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
        }
    }

    private void OnLayoutViewModelChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked += this.OnAssetItemInvoked;
        }
    }
}
