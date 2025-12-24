// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Models;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
/// <param name="currentProject">The current project.</param>
/// <param name="assetCatalog">The asset catalog.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
/// <param name="contentBrowserState">The content browser state to track selection changes.</param>
/// <param name="projectManagerService">The project manager service for creating scenes.</param>
/// <param name="importService">The import service.</param>
/// <param name="windowManagerService">The window manager service.</param>
public partial class AssetsViewModel(
    IProject currentProject,
    IAssetCatalog assetCatalog,
    ViewModelToView vmToViewConverter,
    ContentBrowserState contentBrowserState,
    IProjectManagerService projectManagerService,
    IMessenger messenger,
    IImportService importService,
    IWindowManagerService windowManagerService) : AbstractOutletContainer, IRoutingAware
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
            var assetCount = await assetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);
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

            Debug.WriteLine($"[AssetsViewModel] Requesting navigation to folder: {folder.Location}");

            // Request navigation via the messenger. This allows ProjectLayoutViewModel
            // to handle the navigation, ensuring correct virtual path resolution.
            _ = messenger.Send(new NavigateToFolderRequestMessage(folder));
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

    [RelayCommand]
    private async Task ImportAsync()
    {
        var window = windowManagerService.ActiveWindow?.Window;
        if (window is null)
        {
            return;
        }

        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
        picker.ViewMode = PickerViewMode.List;
        picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
        picker.FileTypeFilter.Add("*");

        var file = await picker.PickSingleFileAsync();
        if (file is null)
        {
            return;
        }

        var projectRoot = contentBrowserState.ProjectRootPath;
        if (string.IsNullOrEmpty(projectRoot))
        {
            Debug.WriteLine("[AssetsViewModel] Project root path is missing.");
            return;
        }

        string relativePath;
        try
        {
            relativePath = Path.GetRelativePath(projectRoot, file.Path);
        }
        catch
        {
            Debug.WriteLine("[AssetsViewModel] File is not in project directory.");
            return;
        }

        if (relativePath.StartsWith("..", StringComparison.Ordinal))
        {
            // File is outside project directory. Copy it to the currently selected folder.
            var destinationFolder = contentBrowserState.SelectedFolders.FirstOrDefault() ?? "Content";
            var fileName = Path.GetFileName(file.Path);
            var destinationPath = Path.Combine(projectRoot, destinationFolder, fileName);

            // Ensure destination directory exists
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);

            try
            {
                File.Copy(file.Path, destinationPath, overwrite: true);
                Debug.WriteLine($"[AssetsViewModel] Copied {file.Path} to {destinationPath}");

                // Update relative path to point to the copied file
                relativePath = Path.GetRelativePath(projectRoot, destinationPath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AssetsViewModel] Failed to copy file: {ex.Message}");
                return;
            }
        }

        relativePath = relativePath.Replace('\\', '/');

        var input = new ImportInput(relativePath, "Content");
        var request = new ImportRequest(projectRoot, [input], new ImportOptions());

        try
        {
            var result = await importService.ImportAsync(request);
            if (result.Succeeded)
            {
                Debug.WriteLine($"[AssetsViewModel] Import succeeded for {relativePath}");
            }
            else
            {
                Debug.WriteLine($"[AssetsViewModel] Import failed for {relativePath}");
                foreach (var diag in result.Diagnostics)
                {
                    Debug.WriteLine($"[Import] {diag.Severity}: {diag.Message}");
                }
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsViewModel] Import exception: {ex}");
        }
    }
}
