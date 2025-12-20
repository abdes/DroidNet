// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
/// <param name="currentProject">The current project.</param>
/// <param name="assetsIndexingService">The service responsible for indexing assets.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
/// <param name="contentBrowserState">The content browser state to track selection changes.</param>
/// <param name="projectManagerService">The project manager service for creating scenes.</param>
public partial class AssetsViewModel(
    IProject currentProject,
    IAssetIndexingService assetsIndexingService,
    ViewModelToView vmToViewConverter,
    ContentBrowserState contentBrowserState,
    IProjectManagerService projectManagerService,
    IMessenger messenger) : AbstractOutletContainer, IRoutingAware
{
    private bool disposed;
    private bool isInitialized;

    /// <summary>
    ///     Gets the layout view model.
    /// </summary>
    public object? LayoutViewModel => this.Outlets["right"].viewModel;

    /// <summary>
    ///     Gets the converter for converting view models to views.
    /// </summary>
    public ViewModelToView VmToViewConverter { get; } = vmToViewConverter;

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // One-time initialization for singleton
        if (!this.isInitialized)
        {
            this.Outlets.Add("right", (nameof(this.LayoutViewModel), null));

            this.PropertyChanging += this.OnLayoutViewModelChanging;
            this.PropertyChanged += this.OnLayoutViewModelChanged;

            // Listen for changes to ContentBrowserState selection via PropertyChanged
            contentBrowserState.PropertyChanged += this.OnContentBrowserStatePropertyChanged;

            // Indexing is started by ContentBrowserViewModel - no need to start here
            this.isInitialized = true;

            // If the project has an active scene, navigate to Scenes folder and request it to be opened
            if (currentProject.ActiveScene is not null)
            {
                // Navigate to the Scenes folder to show scene assets
                contentBrowserState.SetSelectedFolders(["Scenes"]);

                // Request the scene document to be opened
                _ = messenger.Send(new OpenSceneRequestMessage(currentProject.ActiveScene));
            }
        }

        // Asset indexing runs automatically in background with file watching
    }

    /// <summary>
    ///     Releases the unmanaged resources used by the <see cref="AssetsViewModel" /> and optionally releases the managed
    ///     resources.
    /// </summary>
    /// <param name="disposing">
    ///     true to release both managed and unmanaged resources; false to release only unmanaged
    ///     resources.
    /// </param>
    protected new virtual void Dispose(bool disposing)
    {
        if (!this.disposed)
        {
            if (disposing)
            {
                // Cleanup event subscriptions
                contentBrowserState.PropertyChanged -= this.OnContentBrowserStatePropertyChanged;
                this.PropertyChanging -= this.OnLayoutViewModelChanging;
                this.PropertyChanged -= this.OnLayoutViewModelChanged;

                // Cleanup layout view model if necessary
                if (this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
                {
                    layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
                }
            }

            this.disposed = true;
        }
    }

    private async void OnContentBrowserStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            Debug.WriteLine(
                $"[AssetsViewModel] ContentBrowserState.SelectedFolders changed. Selected folders: [{string.Join(", ", contentBrowserState.SelectedFolders)}]");

            // Asset indexing runs automatically in background - no manual refresh needed
            var assetCount = await assetsIndexingService.QueryAssetsAsync().ConfigureAwait(false);
            Debug.WriteLine($"[AssetsViewModel] Assets available: {assetCount.Count}");
        }
    }

    private async void OnAssetItemInvoked(object? sender, AssetsViewItemInvokedEventArgs args)
    {
        _ = sender; // Unused

        Debug.WriteLine(
            $"[AssetsViewModel] Item invoked: {args.InvokedItem.Name}, Type: {args.InvokedItem.AssetType}, Location: {args.InvokedItem.Location}");

        if (args.InvokedItem.AssetType == AssetType.Scene)
        {
            // Update the scene explorer
            var scene = currentProject.Scenes.FirstOrDefault(scene =>
                string.Equals(scene.Name, args.InvokedItem.Name, StringComparison.OrdinalIgnoreCase));
            if (scene is not null)
            {
                currentProject.ActiveScene = scene;

                // Request to open the scene document
                _ = messenger.Send(new OpenSceneRequestMessage(scene));
            }
        }
        else if (args.InvokedItem.AssetType == AssetType.Folder)
        {
            // Navigate into the folder
            Debug.WriteLine($"[AssetsViewModel] Navigating to folder: {args.InvokedItem.Location}");
            await this.NavigateToFolder(args.InvokedItem.Location).ConfigureAwait(false);
        }
    }

    private async Task NavigateToFolder(string folderPath)
    {
        Debug.WriteLine($"[AssetsViewModel] NavigateToFolder called with: {folderPath}");

        try
        {
            // Get the storage provider and create an IFolder instance
            var storageProvider = projectManagerService.GetCurrentProjectStorageProvider();
            var folder = await storageProvider.GetFolderFromPathAsync(folderPath).ConfigureAwait(true);

            var relativePath = folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);
            Debug.WriteLine($"[AssetsViewModel] Navigating to folder: {relativePath}");

            // Update ContentBrowserState directly - this will trigger:
            // 1. ProjectLayoutViewModel to update tree selection via OnContentBrowserStatePropertyChanged
            // 2. AssetsViewModel to refresh via OnContentBrowserStatePropertyChanged
            // 3. Router URL update via ProjectLayoutViewModel.UpdateRouterUrl
            contentBrowserState.SetSelectedFolders([relativePath]);

            Debug.WriteLine($"[AssetsViewModel] ContentBrowserState updated with selected folder: {relativePath}");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsViewModel] Error navigating to folder '{folderPath}': {ex.Message}");
        }
    }

    /// <summary>
    ///     Creates a new scene with the specified name.
    /// </summary>
    /// <param name="sceneName">The name of the new scene.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task CreateNewSceneAsync(string? sceneName)
    {
        if (string.IsNullOrWhiteSpace(sceneName))
        {
            // TODO: Show validation error or prompt for name
            return;
        }

        try
        {
            var newScene = await projectManagerService.CreateSceneAsync(sceneName).ConfigureAwait(true);
            if (newScene is not null)
            {
                // File watcher will automatically detect and index the new scene
                // TODO: Could add success notification here
            }

            // TODO: Show error message to user about scene creation failure
        }
        catch (Exception ex)
        {
            // TODO: Show error message to user
            Debug.WriteLine($"Failed to create scene '{sceneName}': {ex.Message}");
        }
    }

    /// <summary>
    ///     Handles the creation of a new scene by prompting for a name.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task CreateNewSceneWithPromptAsync()
    {
        // For now, create a default name. In a full implementation,
        // this would show a dialog to get the scene name from the user.
        var sceneCount = currentProject.Scenes.Count;
        var defaultName = string.Create(CultureInfo.InvariantCulture, $"NewScene{sceneCount + 1}");

        await this.CreateNewSceneAsync(defaultName).ConfigureAwait(true);
    }

    private void OnLayoutViewModelChanging(object? sender, PropertyChangingEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
        }
    }

    private void OnLayoutViewModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked += this.OnAssetItemInvoked;
        }
    }
}
