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
using Oxygen.Core;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using Oxygen.Storage;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     The ViewModel for the <see cref="AssetsView" /> view.
/// </summary>
/// <param name="assetCatalog">The asset catalog.</param>
/// <param name="vmToViewConverter">The converter for converting view models to views.</param>
/// <param name="contentBrowserState">The content browser state to track selection changes.</param>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="projectManagerService">The project manager service for creating scenes.</param>
/// <param name="storage">The storage provider.</param>
/// <param name="importService">The import service.</param>
/// <param name="windowManagerService">The window manager service.</param>
public partial class AssetsViewModel(
    IAssetCatalog assetCatalog,
    ViewModelToView vmToViewConverter,
    ContentBrowserState contentBrowserState,
    IProjectContextService projectContextService,
    IProjectManagerService projectManagerService,
    IAuthoringTargetResolver authoringTargetResolver,
    IStorageProvider storage,
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

            // If an import/cook completes, the cooked index may update without reliable file watcher events
            // (e.g. cooked folder created after watchers were set up). Force a refresh in that case.
            messenger.Register<AssetsCookedMessage>(this, (_, _) => this.OnAssetsCooked());
            messenger.Register<AssetsChangedMessage>(this, (_, _) => this.OnAssetsChanged());

            // Indexing is started by ContentBrowserViewModel - no need to start here
            this.isInitialized = true;

            // If no browser state was restored, default to the scene assets folder.
            // Workspace activation owns initial scene opening/restoration.
            var activeContext = projectContextService.ActiveProject;
            if (activeContext?.Scenes.Count > 0 && contentBrowserState.SelectedFolders.Count == 0)
            {
                // Navigate to the Scenes folder to show scene assets
                contentBrowserState.SetSelectedFolders(["Content/Scenes"]);
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
                messenger.UnregisterAll(this);

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

    private void OnAssetsCooked()
    {
        if (this.LayoutViewModel is AssetsLayoutViewModel layout)
        {
            _ = layout.RefreshAsync();
        }
    }

    private void OnAssetsChanged()
    {
        if (this.LayoutViewModel is AssetsLayoutViewModel layout)
        {
            _ = layout.RefreshAsync();
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
            $"[AssetsViewModel] Item invoked: {args.InvokedItem.DisplayName}, Kind: {args.InvokedItem.Kind}, URI: {args.InvokedItem.IdentityUri}");

        if (args.InvokedItem.Kind == AssetKind.Scene)
        {
            var currentProject = projectManagerService.CurrentProject;
            if (currentProject is null)
            {
                return;
            }

            // Update the scene explorer
            var scene = currentProject.Scenes.FirstOrDefault(scene =>
                string.Equals(scene.Name, args.InvokedItem.DisplayName, StringComparison.OrdinalIgnoreCase));
            if (scene is not null)
            {
                currentProject.ActiveScene = scene;

                // Request to open the scene document
                _ = messenger.Send(new OpenSceneRequestMessage(scene));
            }
        }
        else if (args.InvokedItem.Kind == AssetKind.Folder)
        {
            // Navigate into the folder
            Debug.WriteLine($"[AssetsViewModel] Navigating to folder: {args.InvokedItem.DisplayPath}");
            await this.NavigateToFolder(args.InvokedItem.DisplayPath).ConfigureAwait(false);
        }
        else if (args.InvokedItem.Kind == AssetKind.Material
                 && args.InvokedItem.IdentityUri.AbsolutePath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            _ = messenger.Send(new OpenMaterialRequestMessage(args.InvokedItem.IdentityUri, args.InvokedItem.DisplayName));
        }
    }

    private async Task NavigateToFolder(string folderPath)
    {
        Debug.WriteLine($"[AssetsViewModel] NavigateToFolder called with: {folderPath}");

        try
        {
            var folder = await storage.GetFolderFromPathAsync(folderPath).ConfigureAwait(true);

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
                projectContextService.Activate(ProjectContext.FromProject(newScene.Project));
                _ = messenger.Send(new AssetsChangedMessage());
                _ = messenger.Send(new OpenSceneRequestMessage(newScene));
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
        var sceneCount = projectContextService.ActiveProject?.Scenes.Count ?? 0;
        var defaultName = string.Create(CultureInfo.InvariantCulture, $"NewScene{sceneCount + 1}");

        await this.CreateNewSceneAsync(defaultName).ConfigureAwait(true);
    }

    public Task CreateNewMaterialAsync(string materialName, string virtualFolder)
    {
        if (!TryNormalizeMaterialName(materialName, out var normalizedName))
        {
            Debug.WriteLine($"[AssetsViewModel] Rejected invalid material name '{materialName}'.");
            return Task.CompletedTask;
        }

        var folder = this.ResolveMaterialFolder(virtualFolder);
        var materialUri = new Uri($"{AssetUris.Scheme}://{folder.TrimEnd('/')}/{normalizedName}.omat.json");
        _ = messenger.Send(new CreateMaterialRequestMessage(materialUri, normalizedName));
        Debug.WriteLine($"[AssetsViewModel] Requested material creation {materialUri}");
        return Task.CompletedTask;
    }

    public string CreateDefaultMaterialName(string virtualFolder)
    {
        var folder = this.ResolveMaterialFolder(virtualFolder);
        var count = this.LayoutViewModel is AssetsLayoutViewModel layout
            ? layout.Assets.Count(asset => asset.Kind == AssetKind.Material)
            : 0;
        var start = Math.Max(1, count + 1);
        for (var i = start; i < start + 1000; i++)
        {
            var candidate = string.Create(CultureInfo.InvariantCulture, $"NewMaterial{i}");
            if (!MaterialSourceExists(folder, candidate))
            {
                return candidate;
            }
        }

        return string.Create(CultureInfo.InvariantCulture, $"NewMaterial{Guid.NewGuid():N}");
    }

    public string GetSelectedMaterialFolder()
        => this.ResolveMaterialFolder(contentBrowserState.SelectedFolders.FirstOrDefault());

    private string ResolveMaterialFolder(string? selected)
    {
        var activeProject = projectContextService.ActiveProject;
        if (activeProject is null)
        {
            return NormalizeMaterialFolder(selected);
        }

        var target = authoringTargetResolver.ResolveCreateTarget(
            activeProject,
            AuthoringAssetKind.Material,
            CreateMaterialTargetSelection(activeProject, selected));
        return target.FolderAssetUri.AbsolutePath;
    }

    internal static ContentBrowserSelection CreateMaterialTargetSelection(ProjectContext activeProject, string? selected)
    {
        var normalized = selected?.Replace('\\', '/').Trim().Trim('/');
        if (string.IsNullOrWhiteSpace(normalized))
        {
            return new ContentBrowserSelection(selected);
        }

        var firstSlash = normalized.IndexOf('/', StringComparison.Ordinal);
        var root = firstSlash < 0 ? normalized : normalized[..firstSlash];
        var localMount = activeProject.LocalFolderMounts.FirstOrDefault(mount =>
            string.Equals(mount.Name, root, StringComparison.OrdinalIgnoreCase));

        return new ContentBrowserSelection(selected, localMount?.Name);
    }

    private void OnLayoutViewModelChanging(object? sender, PropertyChangingEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(this.LayoutViewModel), StringComparison.Ordinal) == true
            && this.LayoutViewModel is AssetsLayoutViewModel layoutViewModel)
        {
            layoutViewModel.ItemInvoked -= this.OnAssetItemInvoked;
        }
    }

    public static string NormalizeMaterialFolder(string? selected, ProjectContext? project = null)
    {
        if (string.IsNullOrWhiteSpace(selected))
        {
            return "/Content/Materials";
        }

        var normalized = selected.Replace('\\', '/').Trim();
        normalized = normalized.TrimEnd('/');
        var normalizedNoRoot = normalized.TrimStart('/');

        if (TryMapSelectedAuthoringFolder(project, normalizedNoRoot, out var mapped))
        {
            return mapped;
        }

        if (normalized.StartsWith('/'))
        {
            return normalized.Equals("/Content", StringComparison.OrdinalIgnoreCase)
                ? "/Content/Materials"
                : normalized.Equals("/Content/Materials", StringComparison.OrdinalIgnoreCase)
                  || normalized.StartsWith("/Content/Materials/", StringComparison.OrdinalIgnoreCase)
                ? normalized
                : "/Content/Materials";
        }

        normalized = normalized.TrimStart('/');
        return normalized.Equals("Content", StringComparison.OrdinalIgnoreCase)
            ? "/Content/Materials"
            : normalized.Equals("Content/Materials", StringComparison.OrdinalIgnoreCase)
              || normalized.StartsWith("Content/Materials/", StringComparison.OrdinalIgnoreCase)
                ? "/" + normalized
                : "/Content/Materials";
    }

    private static bool TryMapSelectedAuthoringFolder(ProjectContext? project, string normalizedNoRoot, out string virtualFolder)
    {
        virtualFolder = string.Empty;
        if (project is null || string.IsNullOrWhiteSpace(normalizedNoRoot))
        {
            return false;
        }

        foreach (var mount in project.AuthoringMounts.OrderByDescending(static mount => mount.RelativePath.Length))
        {
            var mountFolder = mount.RelativePath.Replace('\\', '/').Trim('/');
            if (string.IsNullOrWhiteSpace(mountFolder))
            {
                continue;
            }

            if (normalizedNoRoot.Equals(mountFolder, StringComparison.OrdinalIgnoreCase))
            {
                virtualFolder = "/" + mount.Name + "/Materials";
                return true;
            }

            if (normalizedNoRoot.StartsWith(mountFolder + "/", StringComparison.OrdinalIgnoreCase))
            {
                var mountRelative = normalizedNoRoot[(mountFolder.Length + 1)..];
                virtualFolder = mountRelative.Equals("Materials", StringComparison.OrdinalIgnoreCase)
                                || mountRelative.StartsWith("Materials/", StringComparison.OrdinalIgnoreCase)
                    ? "/" + mount.Name + "/" + mountRelative
                    : "/" + mount.Name + "/Materials";
                return true;
            }
        }

        return false;
    }

    private bool MaterialSourceExists(string virtualFolder, string materialName)
    {
        if (projectContextService.ActiveProject is not { } project || string.IsNullOrWhiteSpace(project.ProjectRoot))
        {
            return false;
        }

        var normalized = virtualFolder.Trim('/').Replace('\\', '/');
        var slash = normalized.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return false;
        }

        var mountName = normalized[..slash];
        var relativeFolder = normalized[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return false;
        }

        var path = Path.Combine(project.ProjectRoot, mount.RelativePath, relativeFolder, materialName + ".omat.json");
        return File.Exists(path);
    }

    private static bool TryNormalizeMaterialName(string materialName, out string normalized)
    {
        normalized = materialName.Trim();
        if (normalized.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            normalized = normalized[..^".omat.json".Length];
        }
        else if (normalized.EndsWith(".omat", StringComparison.OrdinalIgnoreCase))
        {
            normalized = normalized[..^".omat".Length];
        }

        if (string.IsNullOrWhiteSpace(normalized)
            || normalized.Contains('/', StringComparison.Ordinal)
            || normalized.Contains('\\', StringComparison.Ordinal)
            || normalized == "."
            || normalized == ".."
            || normalized.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            normalized = string.Empty;
            return false;
        }

        return true;
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

        if (relativePath.StartsWith("..", StringComparison.Ordinal) || Path.IsPathRooted(relativePath))
        {
            // File is outside project directory. Copy it to the currently selected folder.
            var destinationFolder = contentBrowserState.SelectedFolders.FirstOrDefault() ?? "Content";

            // Ensure destinationFolder is treated as relative to projectRoot by trimming leading slashes.
            // Otherwise, Path.Combine might treat it as an absolute path on the current drive.
            destinationFolder = destinationFolder.TrimStart('/', '\\');

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

        if (Path.IsPathRooted(relativePath))
        {
            Debug.WriteLine($"[AssetsViewModel] Import failed: relative path '{relativePath}' is still absolute. Check project root and destination paths.");
            return;
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
                _ = messenger.Send(new AssetsCookedMessage());
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
