// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Provides services for indexing game assets in the project.
/// </summary>
/// <param name="projectManager">The project manager service.</param>
/// <param name="hostingContext">The hosting context for the application.</param>
/// <param name="contentBrowserState">The content browser state to track selected folders.</param>
public sealed class AssetsIndexingService(IProjectManagerService projectManager, HostingContext hostingContext, ContentBrowserState contentBrowserState) : IDisposable
{
    private readonly Subject<GameAsset> assetSubject = new();
    private IDisposable? subscription;

    /// <summary>
    /// Gets the collection of indexed game assets.
    /// </summary>
    public ObservableCollection<GameAsset> Assets { get; } = [];

    /// <summary>
    /// Starts the asynchronous indexing of game assets based on the current selection.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task IndexAssetsAsync()
    {
        this.subscription = this.assetSubject
            .Buffer(TimeSpan.FromSeconds(1), 5)
            .Where(batch => batch.Count > 0)
            .ObserveOn(hostingContext.DispatcherScheduler)
            .Subscribe(
                batch =>
                {
                    foreach (var asset in batch)
                    {
                        this.Assets.Add(asset);
                    }
                },
                HandleError);

        return Task.CompletedTask;
    }

    /// <summary>
    /// Refreshes the asset collection based on the current folder selection.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task RefreshAssetsAsync()
    {
        this.Assets.Clear();

        // Only load assets if there are selected folders
        if (contentBrowserState.SelectedFolders.Count == 0)
        {
            // No selection = no assets to display
            return;
        }

        // Check if project root is selected (empty path or '.' means root)
        var isProjectRootSelected = contentBrowserState.SelectedFolders.Contains(string.Empty)
                         || contentBrowserState.SelectedFolders.Contains(".");

        if (isProjectRootSelected)
        {
            // For project root selection, show everything: scenes + all file system assets
            // but exclude the Scenes folder from the file system pass to avoid duplicates
            await this.LoadProjectScenesDirect().ConfigureAwait(true);
            await this.LoadFileSystemAssetsDirect(excludeScenesFolder: true).ConfigureAwait(true);
        }
        else
        {
            await this.LoadFileSystemAssetsDirect().ConfigureAwait(true);
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.subscription?.Dispose();
        this.assetSubject.OnCompleted();
        this.assetSubject.Dispose();
    }

    private static void HandleError(Exception ex) =>

        // Handle the error (e.g., log it, show a message to the user, etc.)
        Debug.WriteLine($"Error occurred: {ex.Message}");

    private async Task LoadProjectScenesDirect()
    {
        Debug.Assert(projectManager.CurrentProject is not null, "current project should be initialized");

        try
        {
            foreach (var scene in projectManager.CurrentProject.Scenes)
            {
                var sceneAsset = new GameAsset(scene.Name, $"Scenes/{scene.Name}")
                {
                    AssetType = AssetType.Scene,
                };
                this.Assets.Add(sceneAsset);
            }
        }
        catch (Exception ex)
        {
            HandleError(ex);
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private async Task LoadFileSystemAssetsDirect(bool excludeScenesFolder = false)
    {
        Debug.Assert(projectManager.CurrentProject is not null, "current should be initialized");

        try
        {
            var storageProvider = projectManager.GetCurrentProjectStorageProvider();
            var projectRoot = await storageProvider.GetFolderFromPathAsync(projectManager.CurrentProject.ProjectInfo.Location!).ConfigureAwait(false);

            // If specific folders are selected, index only those folders
            if (contentBrowserState.SelectedFolders.Count > 0 &&
                !contentBrowserState.SelectedFolders.Contains(string.Empty) &&
                !contentBrowserState.SelectedFolders.Contains("."))
            {
                // When explicitly selecting folders, we do not exclude Scenes unless requested
                await this.IndexSelectedFoldersDirect(projectRoot, excludeScenesFolder).ConfigureAwait(false);
            }
            else
            {
                // Index all folders when no specific selection or when project root is selected
                await this.IndexAllFoldersDirect(projectRoot, excludeScenesFolder).ConfigureAwait(false);
            }
        }
        catch (Exception ex)
        {
            HandleError(ex);
        }
    }

    private async Task LoadProjectScenesAsync()
    {
        Debug.Assert(projectManager.CurrentProject is not null, "current project should be initialized");

        try
        {
            foreach (var scene in projectManager.CurrentProject.Scenes)
            {
                var sceneAsset = new GameAsset(scene.Name, $"Scenes/{scene.Name}")
                {
                    AssetType = AssetType.Scene,
                };
                this.assetSubject.OnNext(sceneAsset);
            }
        }
#pragma warning disable CA1031 // exceptions forwarded to subscribers via OnError
        catch (Exception ex)
        {
            this.assetSubject.OnError(ex);
        }
#pragma warning restore CA1031

        await Task.CompletedTask.ConfigureAwait(false);
    }

    private async Task LoadFileSystemAssetsAsync(bool excludeScenesFolder = false)
        => await this.BackgroundIndexerAsync(excludeScenesFolder).ConfigureAwait(true);

    private async Task BackgroundIndexerAsync(bool excludeScenesFolder = false)
    {
        Debug.Assert(projectManager.CurrentProject is not null, "current should be initialized");

        try
        {
            var storageProvider = projectManager.GetCurrentProjectStorageProvider();
            var projectRoot = await storageProvider.GetFolderFromPathAsync(projectManager.CurrentProject.ProjectInfo.Location!).ConfigureAwait(false);

            // If specific folders are selected, index only those folders
            if (contentBrowserState.SelectedFolders.Count > 0 &&
                !contentBrowserState.SelectedFolders.Contains(string.Empty) &&
                !contentBrowserState.SelectedFolders.Contains("."))
            {
                await this.IndexSelectedFoldersAsync(projectRoot, excludeScenesFolder).ConfigureAwait(false);
            }
            else
            {
                // Index all folders when no specific selection or when project root is selected
                await this.IndexAllFoldersAsync(projectRoot, excludeScenesFolder).ConfigureAwait(false);
            }
        }
#pragma warning disable CA1031 // exceptions forwarded to subscribers via OnError
        catch (Exception ex)
        {
            this.assetSubject.OnError(ex);
        }
#pragma warning restore CA1031
    }

    private async Task IndexSelectedFoldersDirect(IFolder projectRoot, bool excludeScenesFolder = false)
    {
        foreach (var selectedPath in contentBrowserState.SelectedFolders)
        {
            if (string.IsNullOrEmpty(selectedPath) || selectedPath == ".")
            {
                continue; // Skip empty path (project root) for file system indexing
            }

            if (excludeScenesFolder && IsScenesPath(selectedPath))
            {
                continue; // Skip Scenes folder to avoid duplicates when root selection combines sources
            }

            try
            {
                var selectedFolder = await projectRoot.GetFolderAsync(selectedPath).ConfigureAwait(false);
                if (await selectedFolder.ExistsAsync().ConfigureAwait(false))
                {
                    await this.IndexFolderDirect(selectedFolder).ConfigureAwait(false);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to index selected folder '{selectedPath}': {ex.Message}");
            }
        }
    }

    private async Task IndexAllFoldersDirect(IFolder rootFolder, bool excludeScenesFolder = false)
    {
        var folderQueue = new Queue<IFolder>();
        folderQueue.Enqueue(rootFolder);

        while (folderQueue.Count > 0)
        {
            var currentFolder = folderQueue.Dequeue();
            await this.IndexFolderDocumentsOnlyDirect(currentFolder).ConfigureAwait(false);

            await foreach (var subFolder in currentFolder.GetFoldersAsync().ConfigureAwait(false))
            {
                if (excludeScenesFolder && string.Equals(subFolder.Name, "Scenes", StringComparison.OrdinalIgnoreCase))
                {
                    continue; // Skip Scenes folder to avoid duplicates when root is selected
                }

                folderQueue.Enqueue(subFolder);
            }
        }
    }

    private async Task IndexFolderDocumentsOnlyDirect(IFolder folder)
    {
        // Add only documents as file assets (no subfolders for recursive indexing)
        await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
        {
            var assetName = document.Name.Contains('.', StringComparison.Ordinal)
                ? document.Name[..document.Name.LastIndexOf('.')]
                : document.Name;
            if (string.IsNullOrEmpty(assetName))
            {
                continue;
            }

            var asset = new GameAsset(assetName, document.Location)
            {
                AssetType = GameAsset.GetAssetType(document.Name),
            };
            this.Assets.Add(asset);
        }
    }

    private async Task IndexFolderDirect(IFolder folder)
    {
        // Add subfolders as folder assets
        await foreach (var subfolder in folder.GetFoldersAsync().ConfigureAwait(false))
        {
            var folderAsset = new GameAsset(subfolder.Name, subfolder.Location, AssetType.Folder);
            this.Assets.Add(folderAsset);
        }

        // Add documents as file assets
        await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
        {
            var assetName = document.Name.Contains('.', StringComparison.Ordinal)
                ? document.Name[..document.Name.LastIndexOf('.')]
                : document.Name;
            if (string.IsNullOrEmpty(assetName))
            {
                continue;
            }

            var asset = new GameAsset(assetName, document.Location)
            {
                AssetType = GameAsset.GetAssetType(document.Name),
            };
            this.Assets.Add(asset);
        }
    }

    private async Task IndexSelectedFoldersAsync(IFolder projectRoot, bool excludeScenesFolder = false)
    {
        foreach (var selectedPath in contentBrowserState.SelectedFolders)
        {
            if (string.IsNullOrEmpty(selectedPath) || selectedPath == ".")
            {
                continue; // Skip empty path (project root) for file system indexing
            }

            if (excludeScenesFolder && IsScenesPath(selectedPath))
            {
                continue; // Skip Scenes folder to avoid duplicates when root selection combines sources
            }

            try
            {
                var selectedFolder = await projectRoot.GetFolderAsync(selectedPath).ConfigureAwait(false);
                if (await selectedFolder.ExistsAsync().ConfigureAwait(false))
                {
                    await this.IndexFolderAsync(selectedFolder).ConfigureAwait(false);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to index selected folder '{selectedPath}': {ex.Message}");
            }
        }
    }

    private async Task IndexAllFoldersAsync(IFolder rootFolder, bool excludeScenesFolder = false)
    {
        var folderQueue = new Queue<IFolder>();
        folderQueue.Enqueue(rootFolder);

        while (folderQueue.Count > 0)
        {
            var currentFolder = folderQueue.Dequeue();
            await this.IndexFolderDocumentsOnlyAsync(currentFolder).ConfigureAwait(false);

            await foreach (var subFolder in currentFolder.GetFoldersAsync().ConfigureAwait(false))
            {
                if (excludeScenesFolder && string.Equals(subFolder.Name, "Scenes", StringComparison.OrdinalIgnoreCase))
                {
                    continue; // Skip Scenes folder to avoid duplicates when root is selected
                }

                folderQueue.Enqueue(subFolder);
            }
        }
    }

    private static bool IsScenesPath(string relativePath)
        => relativePath.Equals("Scenes", StringComparison.OrdinalIgnoreCase)
           || relativePath.StartsWith("Scenes/", StringComparison.OrdinalIgnoreCase)
           || relativePath.StartsWith("Scenes\\", StringComparison.OrdinalIgnoreCase);

    private async Task IndexFolderDocumentsOnlyAsync(IFolder folder)
    {
        // Add only documents as file assets (no subfolders for recursive indexing)
        await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
        {
            var assetName = document.Name.Contains('.', StringComparison.Ordinal)
                ? document.Name[..document.Name.LastIndexOf('.')]
                : document.Name;
            if (string.IsNullOrEmpty(assetName))
            {
                continue;
            }

            var asset = new GameAsset(assetName, document.Location)
            {
                AssetType = GameAsset.GetAssetType(document.Name),
            };
            this.assetSubject.OnNext(asset);
        }
    }

    private async Task IndexFolderAsync(IFolder folder)
    {
        // Add subfolders as folder assets
        await foreach (var subfolder in folder.GetFoldersAsync().ConfigureAwait(false))
        {
            var folderAsset = new GameAsset(subfolder.Name, subfolder.Location, AssetType.Folder);
            this.assetSubject.OnNext(folderAsset);
        }

        // Add documents as file assets
        await foreach (var document in folder.GetDocumentsAsync().ConfigureAwait(false))
        {
            var assetName = document.Name.Contains('.', StringComparison.Ordinal)
                ? document.Name[..document.Name.LastIndexOf('.')]
                : document.Name;
            if (string.IsNullOrEmpty(assetName))
            {
                continue;
            }

            var asset = new GameAsset(assetName, document.Location)
            {
                AssetType = GameAsset.GetAssetType(document.Name),
            };
            this.assetSubject.OnNext(asset);
        }
    }
}
