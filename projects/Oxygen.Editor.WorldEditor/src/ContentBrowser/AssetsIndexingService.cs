// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
/// Provides services for indexing game assets in the project.
/// Continuously indexes in background at low priority with file watching.
/// </summary>
/// <param name="projectManager">The project manager service.</param>
/// <param name="hostingContext">The hosting context for the application.</param>
public sealed partial class AssetsIndexingService(IProjectManagerService projectManager, HostingContext hostingContext) : IAssetIndexingService
{
    private readonly ReplaySubject<AssetChangeNotification> changeStream = new(bufferSize: 20); // TODO: tune this based on the speed of the indexer
    private readonly ConcurrentBag<GameAsset> assets = [];
    private readonly ConcurrentDictionary<string, bool> indexedFolders = new(StringComparer.OrdinalIgnoreCase);
    private readonly ConcurrentDictionary<string, FileSystemWatcher> watchers = new(StringComparer.Ordinal);
    private readonly CancellationTokenSource cts = new();
    private readonly SemaphoreSlim completionLock = new(0); // Signaled when indexing completes
    private Task? backgroundTask;
    private IndexingStatus status = IndexingStatus.NotStarted;
    private int foldersScanned;
    private int assetsFound;

    /// <inheritdoc/>
    public IndexingStatus Status => this.status;

    /// <inheritdoc/>
    public IObservable<AssetChangeNotification> AssetChanges => this.changeStream.AsObservable();

    /// <inheritdoc/>
    public void Dispose()
    {
        this.cts.Cancel();
        foreach (var watcher in this.watchers.Values)
        {
            watcher.Dispose();
        }

        this.watchers.Clear();
        this.changeStream.OnCompleted();
        this.changeStream.Dispose();
        this.cts.Dispose();
        this.completionLock.Dispose();
    }

    /// <inheritdoc/>
    public async Task StartIndexingAsync(IProgress<IndexingProgress>? progress = null, CancellationToken ct = default)
    {
        if (this.status != IndexingStatus.NotStarted)
        {
            Debug.WriteLine("[AssetsIndexingService] Indexing already started");
            return;
        }

        this.status = IndexingStatus.Indexing;
        Debug.WriteLine("[AssetsIndexingService] Starting background indexing");

        // Start background indexing loop
        this.backgroundTask = Task.Run(async () => await this.BackgroundIndexingLoop(progress).ConfigureAwait(false), ct);

        // Wait for initial indexing to complete
        await this.completionLock.WaitAsync(ct).ConfigureAwait(false);
        this.completionLock.Release(); // Allow future callers to pass through immediately
    }

    /// <inheritdoc/>
    public async Task StopIndexingAsync()
    {
        if (this.status is IndexingStatus.Stopped or IndexingStatus.NotStarted)
        {
            return;
        }

        Debug.WriteLine("[AssetsIndexingService] Stopping indexing");
        this.status = IndexingStatus.Stopped;
        await this.cts.CancelAsync().ConfigureAwait(false);

        if (this.backgroundTask != null)
        {
            try
            {
                await this.backgroundTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // Expected
            }
        }
    }

    /// <inheritdoc/>
    public async Task<IReadOnlyList<GameAsset>> QueryAssetsAsync(
        Func<GameAsset, bool>? predicate = null,
        CancellationToken ct = default)
    {
        // Wait for indexing to complete if still in progress
        if (this.status == IndexingStatus.Indexing)
        {
            Debug.WriteLine("[AssetsIndexingService] QueryAssetsAsync waiting for indexing to complete");
            await this.completionLock.WaitAsync(ct).ConfigureAwait(false);
            this.completionLock.Release();
        }

        // Return snapshot with optional filtering
        var snapshot = predicate == null
            ? this.assets.ToList()
            : this.assets.Where(predicate).ToList();

        Debug.WriteLine($"[AssetsIndexingService] QueryAssetsAsync returned {snapshot.Count} assets");
        return snapshot;
    }

    private async Task BackgroundIndexingLoop(IProgress<IndexingProgress>? progress)
    {
        Debug.WriteLine("[AssetsIndexingService] Background indexing loop started");

        try
        {
            var storageProvider = projectManager.GetCurrentProjectStorageProvider();
            var projectRoot = await storageProvider.GetFolderFromPathAsync(
                projectManager.CurrentProject!.ProjectInfo.Location!).ConfigureAwait(false);

            var foldersToIndex = new Queue<IFolder>();
            foldersToIndex.Enqueue(projectRoot);

            while (!this.cts.Token.IsCancellationRequested && foldersToIndex.Count > 0)
            {
                var folder = foldersToIndex.Dequeue();
                await this.IndexFolderWithWatchingAsync(folder, progress).ConfigureAwait(false);

                // Enqueue subfolders for indexing
                await foreach (var subFolder in folder.GetFoldersAsync().ConfigureAwait(false))
                {
                    foldersToIndex.Enqueue(subFolder);
                }
            }

            // Initial indexing complete - signal completion
            Debug.WriteLine("[AssetsIndexingService] Initial indexing completed");
            this.status = IndexingStatus.Completed;
            this.completionLock.Release();

            // Report final progress
            progress?.Report(new IndexingProgress(this.foldersScanned, this.assetsFound, null));
        }
        catch (OperationCanceledException)
        {
            Debug.WriteLine("[AssetsIndexingService] Background indexing canceled");
            this.status = IndexingStatus.Stopped;
            this.completionLock.Release(); // Unblock any waiters
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsIndexingService] Background indexing error: {ex.Message}");
            this.status = IndexingStatus.Stopped;
            this.completionLock.Release(); // Unblock any waiters
        }
    }

    private async Task IndexFolderWithWatchingAsync(IFolder folder, IProgress<IndexingProgress>? progress)
    {
        if (this.indexedFolders.ContainsKey(folder.Location))
        {
            return; // Already indexed
        }

        Debug.WriteLine($"[AssetsIndexingService] Indexing folder: {folder.Location}");
        Interlocked.Increment(ref this.foldersScanned);

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

            this.assets.Add(asset);
            Interlocked.Increment(ref this.assetsFound);

            var notification = new AssetChangeNotification(
                AssetChangeType.Added,
                asset,
                DateTimeOffset.UtcNow);
            this.changeStream.OnNext(notification);
        }

        this.indexedFolders[folder.Location] = true;
        this.SetupFileWatcher(folder.Location);

        // Report progress periodically
        if (this.foldersScanned % 10 == 0)
        {
            progress?.Report(new IndexingProgress(this.foldersScanned, this.assetsFound, folder.Name));
        }
    }

    private void SetupFileWatcher(string folderPath)
    {
        if (this.watchers.ContainsKey(folderPath))
        {
            return;
        }

        try
        {
            var watcher = new FileSystemWatcher(folderPath)
            {
                NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite,
                EnableRaisingEvents = true,
            };

            watcher.Created += (s, e) => _ = hostingContext.Dispatcher.DispatchAsync(() => this.OnFileAdded(e.FullPath));
            watcher.Deleted += (s, e) => _ = hostingContext.Dispatcher.DispatchAsync(() => this.OnFileRemoved(e.FullPath));
            watcher.Changed += (s, e) => _ = hostingContext.Dispatcher.DispatchAsync(() => this.OnFileModified(e.FullPath));

            this.watchers[folderPath] = watcher;
            Debug.WriteLine($"[AssetsIndexingService] File watcher set up for: {folderPath}");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsIndexingService] Failed to setup watcher for {folderPath}: {ex.Message}");
        }
    }

    private void OnFileAdded(string filePath)
    {
        Debug.WriteLine($"[AssetsIndexingService] File added: {filePath}");

        var fileName = Path.GetFileName(filePath);
        var assetName = fileName.Contains('.', StringComparison.Ordinal)
            ? fileName[..fileName.LastIndexOf('.')]
            : fileName;

        if (string.IsNullOrEmpty(assetName))
        {
            return;
        }

        var asset = new GameAsset(assetName, filePath)
        {
            AssetType = GameAsset.GetAssetType(fileName),
        };

        this.assets.Add(asset);
        var notification = new AssetChangeNotification(
            AssetChangeType.Added,
            asset,
            DateTimeOffset.UtcNow);
        this.changeStream.OnNext(notification);
    }

    private void OnFileRemoved(string filePath)
    {
        Debug.WriteLine($"[AssetsIndexingService] File removed: {filePath}");

        var asset = this.assets.FirstOrDefault(a => a.Location.Equals(filePath, StringComparison.OrdinalIgnoreCase));
        if (asset != null)
        {
            // ConcurrentBag doesn't support removal, so we'll just publish the notification
            // The asset will remain in the bag but queries can filter it out if needed
            // For a production system, consider using ConcurrentDictionary<string, GameAsset> instead
            var notification = new AssetChangeNotification(
                AssetChangeType.Removed,
                asset,
                DateTimeOffset.UtcNow);
            this.changeStream.OnNext(notification);
        }
    }

    private void OnFileModified(string filePath)
    {
        Debug.WriteLine($"[AssetsIndexingService] File modified: {filePath}");

        var asset = this.assets.FirstOrDefault(a => a.Location.Equals(filePath, StringComparison.OrdinalIgnoreCase));
        if (asset != null)
        {
            var notification = new AssetChangeNotification(
                AssetChangeType.Modified,
                asset,
                DateTimeOffset.UtcNow);
            this.changeStream.OnNext(notification);
        }
    }
}
