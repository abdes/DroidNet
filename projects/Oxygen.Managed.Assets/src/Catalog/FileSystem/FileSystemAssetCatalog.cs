// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Managed.Assets.Catalog.FileSystem;

/// <summary>
/// A catalog provider backed by a filesystem folder tree ("loose" assets).
/// </summary>
/// <remarks>
/// <para>
/// This catalog enumerates files under a configured root folder and maps them to canonical asset URIs.
/// </para>
/// <para>
/// It uses an Rx pipeline for filesystem event ingestion and emits <see cref="AssetChange"/> deltas after
/// applying changes to an internal keyed store.
/// </para>
/// </remarks>
public sealed partial class FileSystemAssetCatalog : IAssetCatalog, IRefreshableAssetCatalog, IAssetFolderSource, IDisposable
{
    private readonly IStorageProvider storage;
    private readonly FileSystemAssetCatalogOptions options;
    private readonly IFileSystemCatalogEventSource eventSource;

    private readonly Subject<AssetChange> changes = new();
    private readonly ConcurrentDictionary<Uri, AssetRecord> records = new();
    private readonly Subject<Uri> folderChanges = new();

    // The folders under the root, by full path; a watcher reports a folder event without saying it is a folder.
    private readonly ConcurrentDictionary<string, Uri> folders = new(StringComparer.OrdinalIgnoreCase);
    private readonly IDisposable eventSubscription;
    private readonly ILogger logger;
    private readonly Lock batchSync = new();
    private Task applying = Task.CompletedTask;
    private Tally reported;

    private volatile bool isInitialized;
    private IFolder? rootFolder;

    /// <summary>
    /// Initializes a new instance of the <see cref="FileSystemAssetCatalog"/> class.
    /// </summary>
    /// <param name="storage">The storage provider used for snapshot enumeration.</param>
    /// <param name="options">Provider options.</param>
    /// <param name="logger">Records the changes each batch of file events reported, and each rescan.</param>
    public FileSystemAssetCatalog(IStorageProvider storage, FileSystemAssetCatalogOptions options, ILogger? logger = null)
        : this(storage, options, eventSource: null, logger)
    {
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "MA0015:Specify the parameter name in ArgumentException", Justification = "valid use")]
    internal FileSystemAssetCatalog(
        IStorageProvider storage,
        FileSystemAssetCatalogOptions options,
        IFileSystemCatalogEventSource? eventSource,
        ILogger? logger = null)
    {
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentNullException.ThrowIfNull(options);
        this.logger = logger ?? NullLogger.Instance;
        ArgumentException.ThrowIfNullOrEmpty(options.MountPoint);
        ArgumentException.ThrowIfNullOrEmpty(options.RootFolderPath);

        this.storage = storage;
        this.options = options;

        // Watching is optional in tests via injected source. The catalog may
        // be created for generated project folders that do not exist yet
        // (.cooked, .imported, .build). Snapshot enumeration already treats a
        // missing root as empty, so construction must not require the exact
        // root directory to exist either.
        if (eventSource is not null)
        {
            this.eventSource = eventSource;
        }
        else
        {
            var watchRoot = FindNearestExistingDirectory(
                this.storage.Normalize(options.RootFolderPath));
            this.eventSource = string.IsNullOrEmpty(watchRoot)
                ? new NoopFileSystemCatalogEventSource()
                : new FileSystemWatcherEventSource(
                    rootPath: watchRoot,
                    filter: options.WatcherFilter);
        }

        // Coalesce bursts to avoid watcher noise (rename often appears as many events).
        this.eventSubscription = this.eventSource.Events
            .Buffer(TimeSpan.FromMilliseconds(100))
            .Where(batch => batch.Count > 0)
            .Subscribe(batch => this.QueueBatch(batch.AsReadOnly()));
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    public IObservable<Uri> FolderChanges => this.folderChanges.AsObservable();

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "LINQ is more clear like that")]
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);

        IEnumerable<AssetRecord> results = this.records.Values;
        results = results.Where(r => AssetQueryScopeMatcher.IsMatch(query.Scope, r.Uri));

        if (!string.IsNullOrWhiteSpace(query.SearchText))
        {
            var term = query.SearchText.Trim();
            results = results.Where(r => MatchesSearch(r, term));
        }

        return results
            .OrderBy(r => r.Uri.ToString(), StringComparer.Ordinal)
            .ToArray();
    }

    /// <inheritdoc />
    /// <remarks>
    /// The changes the refresh finds are reported like the watcher's, so a refresh that runs before the watcher's
    /// events arrive does not leave those changes unreported: the events then find nothing left to change.
    /// </remarks>
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await this.RescanAsync(publishChanges: true, eventsLost: false, cancellationToken).ConfigureAwait(false);
    }

    public void Dispose()
    {
        this.eventSubscription.Dispose();
        this.eventSource.Dispose();
        this.changes.OnCompleted();
        this.changes.Dispose();
        this.folderChanges.OnCompleted();
        this.folderChanges.Dispose();
    }

    private static bool MatchesSearch(AssetRecord record, string term)
        => record.Name.Contains(term, StringComparison.OrdinalIgnoreCase)
            || record.Uri.ToString().Contains(term, StringComparison.OrdinalIgnoreCase)
            || AssetUriHelper.GetMountPoint(record.Uri).Contains(term, StringComparison.OrdinalIgnoreCase);

    // A path under the root that the catalog lists: not hidden, and not in a folder it was told to leave out.
    private bool Includes(string relativePath)
        => ShouldInclude(relativePath)
            && !this.options.ExcludedFolders.Any(excluded => relativePath.Equals(excluded, StringComparison.OrdinalIgnoreCase)
                || relativePath.StartsWith(excluded + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
                || relativePath.StartsWith(excluded + Path.AltDirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));

    private static bool ShouldInclude(string relativePath)
    {
        if (string.Equals(relativePath, ".", StringComparison.Ordinal))
        {
            return true;
        }

        var segments = relativePath.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        foreach (var segment in segments)
        {
            if (string.Equals(segment, ".", StringComparison.Ordinal))
            {
                continue;
            }

            if (string.Equals(segment, "..", StringComparison.Ordinal))
            {
                return false;
            }

            if (segment.Length > 0 && segment[0] == '.')
            {
                return false;
            }
        }

        return true;
    }

    private static string FindNearestExistingDirectory(string start)
    {
        if (string.IsNullOrWhiteSpace(start))
        {
            return string.Empty;
        }

        var current = start;
        while (!string.IsNullOrEmpty(current))
        {
            if (Directory.Exists(current))
            {
                return current;
            }

            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent) || string.Equals(parent, current, StringComparison.Ordinal))
            {
                break;
            }

            current = parent;
        }

        return string.Empty;
    }

    private async Task EnsureInitializedAsync(CancellationToken cancellationToken)
    {
        if (this.isInitialized)
        {
            return;
        }

        var folder = await this.storage.GetFolderFromPathAsync(this.options.RootFolderPath, cancellationToken)
            .ConfigureAwait(false);

        this.rootFolder = folder;

        // If the root does not exist, treat it as empty.
        if (!await folder.ExistsAsync().ConfigureAwait(false))
        {
            this.isInitialized = true;
            return;
        }

        var snapshot = await this.EnumerateAllFilesAsync(folder, cancellationToken).ConfigureAwait(false);
        foreach (var record in snapshot.Records)
        {
            _ = this.records.TryAdd(record.Uri, record);
        }

        foreach (var (path, uri) in snapshot.Folders)
        {
            _ = this.folders.TryAdd(path, uri);
        }

        this.isInitialized = true;
    }

    private async Task<Snapshot> EnumerateAllFilesAsync(IFolder root, CancellationToken cancellationToken)
    {
        var results = new List<AssetRecord>();
        var found = new Dictionary<string, Uri>(StringComparer.OrdinalIgnoreCase);
        var pending = new Stack<IFolder>();
        pending.Push(root);

        while (pending.Count > 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var folder = pending.Pop();

            await foreach (var document in folder.GetDocumentsAsync(cancellationToken).ConfigureAwait(false))
            {
                var relative = Path.GetRelativePath(root.Location, document.Location);
                if (this.Includes(relative))
                {
                    var uri = this.MapFilePathToAssetUri(root.Location, document.Location);
                    results.Add(new AssetRecord(uri));
                }
            }

            await foreach (var childFolder in folder.GetFoldersAsync(cancellationToken).ConfigureAwait(false))
            {
                var relative = Path.GetRelativePath(root.Location, childFolder.Location);
                if (this.Includes(relative))
                {
                    found[childFolder.Location] = this.MapFilePathToAssetUri(root.Location, childFolder.Location);
                    pending.Push(childFolder);
                }
            }
        }

        return new(results, found);
    }

    private Uri MapFilePathToAssetUri(string rootLocation, string fullPath)
    {
        var relative = Path.GetRelativePath(rootLocation, fullPath);
        return AssetUriHelper.CreateUri(this.options.MountPoint, relative);
    }

    // Batches apply one at a time, in the order the watcher reported them: a rescan that read the disk before a delete
    // must not finish after the rescan that read it after, or it would bring back what was deleted.
    private void QueueBatch(IReadOnlyList<FileSystemCatalogEvent> batch)
    {
        lock (this.batchSync)
        {
            this.applying = this.applying
                .ContinueWith(_ => this.ApplyBatchAsync(batch), CancellationToken.None, TaskContinuationOptions.None, TaskScheduler.Default)
                .Unwrap();
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A batch runs off the watcher with no caller to report to; its failure is logged instead of lost.")]
    private async Task ApplyBatchAsync(IReadOnlyList<FileSystemCatalogEvent> batch)
    {
        this.reported = default;
        try
        {
            await this.ApplyEventsAsync(batch).ConfigureAwait(false);
        }
        catch (Exception failure)
        {
            this.LogBatchFailed(failure, batch.Count, this.options.MountPoint);
        }
        finally
        {
            if (this.reported.Total != 0)
            {
                this.LogChangesReported(batch.Count, this.options.MountPoint, this.reported.Added, this.reported.Removed, this.reported.Updated, this.reported.Relocated);
            }
        }
    }

    private async Task ApplyEventsAsync(IReadOnlyList<FileSystemCatalogEvent> batch)
    {
        try
        {
            // Ensure we have an initial baseline before applying deltas.
            await this.EnsureInitializedAsync(CancellationToken.None).ConfigureAwait(false);
            if (this.rootFolder is null)
            {
                return;
            }

            // A folder event stands for every file inside it, which the watcher does not report one by one.
            if (await this.HasFolderEventAsync(batch).ConfigureAwait(false))
            {
                this.LogFolderRescan(this.options.MountPoint);
                await this.RescanAsync(publishChanges: true, eventsLost: false, CancellationToken.None).ConfigureAwait(false);
                return;
            }

            foreach (var ev in batch)
            {
                switch (ev.Kind)
                {
                    case FileSystemCatalogEventKind.RescanRequired:
                        this.LogEventsLost(this.options.MountPoint);
                        await this.RescanAsync(publishChanges: true, eventsLost: true, CancellationToken.None).ConfigureAwait(false);
                        break;

                    case FileSystemCatalogEventKind.Renamed:
                        this.ApplyRenamed(ev);
                        break;

                    case FileSystemCatalogEventKind.Created:
                        this.ApplyCreated(ev);
                        break;

                    case FileSystemCatalogEventKind.Changed when !this.folders.ContainsKey(ev.FullPath):
                        this.ApplyChanged(ev);
                        break;

                    case FileSystemCatalogEventKind.Deleted:
                        this.ApplyDeleted(ev);
                        break;
                }
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidPathException or StorageException)
        {
            // If anything goes wrong while applying deltas, fall back to a full rescan.
            this.LogApplyFailed(ex, this.options.MountPoint);
            await this.RescanAsync(publishChanges: true, eventsLost: true, CancellationToken.None).ConfigureAwait(false);
        }
    }

    private void ApplyCreated(FileSystemCatalogEvent ev)
    {
        if (!this.TryMapIfUnderRoot(ev.FullPath, out var uri))
        {
            return;
        }

        var record = new AssetRecord(uri);
        if (this.records.TryAdd(uri, record))
        {
            this.Publish(new AssetChange(AssetChangeKind.Added, uri));
        }
    }

    private void ApplyChanged(FileSystemCatalogEvent ev)
    {
        if (!this.TryMapIfUnderRoot(ev.FullPath, out var uri))
        {
            return;
        }

        // If we don't know the file yet, treat as add.
        var record = new AssetRecord(uri);
        var isNew = this.records.TryAdd(uri, record);
        this.Publish(new AssetChange(isNew ? AssetChangeKind.Added : AssetChangeKind.Updated, uri));
    }

    private void ApplyDeleted(FileSystemCatalogEvent ev)
    {
        if (!this.TryMapIfUnderRoot(ev.FullPath, out var uri))
        {
            return;
        }

        if (this.records.TryRemove(uri, out _))
        {
            this.Publish(new AssetChange(AssetChangeKind.Removed, uri));
        }
    }

    private void ApplyRenamed(FileSystemCatalogEvent ev)
    {
        if (string.IsNullOrEmpty(ev.OldFullPath))
        {
            this.ApplyChanged(ev);
            return;
        }

        if (!this.TryMapIfUnderRoot(ev.OldFullPath, out var oldUri))
        {
            // Old was outside scope or hidden, as the temporary file of an atomic save is; new might be inside, and
            // replacing a known file changes it.
            this.ApplyChanged(ev);
            return;
        }

        if (!this.TryMapIfUnderRoot(ev.FullPath, out var newUri))
        {
            // New is outside scope; old was inside.
            this.ApplyDeleted(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Deleted, ev.OldFullPath));
            return;
        }

        if (this.records.TryRemove(oldUri, out _))
        {
            _ = this.records.TryAdd(newUri, new AssetRecord(newUri));
            this.Publish(new AssetChange(AssetChangeKind.Relocated, newUri, PreviousUri: oldUri));
        }
        else
        {
            // If we didn't know the old key, the new file appeared or replaced a known one.
            this.ApplyChanged(ev);
        }
    }

    // Batches apply one at a time, so the tally of the batch being applied needs no lock.
    private void Publish(AssetChange change)
    {
        this.reported = this.reported.Add(change.Kind);
        this.changes.OnNext(change);
    }

    private bool TryMapIfUnderRoot(string fullPath, out Uri uri)
    {
        uri = null!;
        if (this.rootFolder is null)
        {
            return false;
        }

        var root = this.rootFolder.Location;
        if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var relative = Path.GetRelativePath(root, fullPath);
        if (!this.Includes(relative))
        {
            return false;
        }

        uri = this.MapFilePathToAssetUri(root, fullPath);
        return true;
    }

    // After lost watcher events, a file the catalog already knew may have changed unreported, so each is reported as
    // updated; after a folder event, only what appeared or disappeared is reported.
    private async Task RescanAsync(bool publishChanges, bool eventsLost, CancellationToken cancellationToken)
    {
        if (this.rootFolder is null)
        {
            return;
        }

        if (!await this.rootFolder.ExistsAsync().ConfigureAwait(false))
        {
            foreach (var existing in this.records.Keys)
            {
                if (this.records.TryRemove(existing, out _) && publishChanges)
                {
                    this.Publish(new AssetChange(AssetChangeKind.Removed, existing));
                }
            }

            this.ReplaceFolders(new Dictionary<string, Uri>(StringComparer.OrdinalIgnoreCase), publishChanges);
            return;
        }

        var snapshot = await this.EnumerateAllFilesAsync(this.rootFolder, cancellationToken).ConfigureAwait(false);
        var next = snapshot.Records.ToDictionary(r => r.Uri, r => r);

        // Removed
        foreach (var existing in this.records.Keys)
        {
            if (!next.ContainsKey(existing) && this.records.TryRemove(existing, out _))
            {
                if (publishChanges)
                {
                    this.Publish(new AssetChange(AssetChangeKind.Removed, existing));
                }
            }
        }

        // Added, and when reporting, updated: a rescan follows lost watcher events, so a file it already knew may have
        // changed without being reported.
        foreach (var kvp in next)
        {
            if (this.records.TryAdd(kvp.Key, kvp.Value))
            {
                if (publishChanges)
                {
                    this.Publish(new AssetChange(AssetChangeKind.Added, kvp.Key));
                }
            }
            else if (publishChanges && eventsLost)
            {
                this.Publish(new AssetChange(AssetChangeKind.Updated, kvp.Key));
            }
        }

        // Folders are reported after their files, so a view that reloads on a folder change finds the files in place.
        this.ReplaceFolders(snapshot.Folders, publishChanges);
    }

    // Called with the folders a scan found: reports each folder that appeared or disappeared since the last scan.
    private void ReplaceFolders(IReadOnlyDictionary<string, Uri> found, bool publishChanges)
    {
        foreach (var (path, uri) in this.folders.ToArray())
        {
            if (!found.ContainsKey(path) && this.folders.TryRemove(path, out _) && publishChanges)
            {
                this.folderChanges.OnNext(uri);
            }
        }

        foreach (var (path, uri) in found)
        {
            if (this.folders.TryAdd(path, uri) && publishChanges)
            {
                this.folderChanges.OnNext(uri);
            }
        }
    }

    private async Task<bool> HasFolderEventAsync(IReadOnlyList<FileSystemCatalogEvent> batch)
    {
        foreach (var ev in batch)
        {
            // A folder's Changed event only says that its contents changed; the files report themselves.
            if (ev.Kind is FileSystemCatalogEventKind.Changed)
            {
                continue;
            }

            if (this.folders.ContainsKey(ev.FullPath) || (ev.OldFullPath is { } old && this.folders.ContainsKey(old)))
            {
                return true;
            }

            if (ev.Kind is FileSystemCatalogEventKind.Created or FileSystemCatalogEventKind.Renamed
                && this.TryMapIfUnderRoot(ev.FullPath, out _)
                && await this.storage.FolderExistsAsync(ev.FullPath).ConfigureAwait(false))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>The changes one batch of file events reported, by kind.</summary>
    /// <param name="Added">The files added.</param>
    /// <param name="Removed">The files removed.</param>
    /// <param name="Updated">The files updated.</param>
    /// <param name="Relocated">The files renamed or moved under the root.</param>
    private readonly record struct Tally(int Added, int Removed, int Updated, int Relocated)
    {
        public int Total => this.Added + this.Removed + this.Updated + this.Relocated;

        public Tally Add(AssetChangeKind kind) => kind switch
        {
            AssetChangeKind.Added => this with { Added = this.Added + 1 },
            AssetChangeKind.Removed => this with { Removed = this.Removed + 1 },
            AssetChangeKind.Relocated => this with { Relocated = this.Relocated + 1 },
            _ => this with { Updated = this.Updated + 1 },
        };
    }

    /// <summary>The files and folders under the root.</summary>
    /// <param name="Records">The files.</param>
    /// <param name="Folders">The folders, by full path.</param>
    private sealed record Snapshot(IReadOnlyList<AssetRecord> Records, IReadOnlyDictionary<string, Uri> Folders);

    private sealed class NoopFileSystemCatalogEventSource : IFileSystemCatalogEventSource
    {
        public IObservable<FileSystemCatalogEvent> Events { get; } = Observable.Empty<FileSystemCatalogEvent>();

        public void Dispose()
        {
        }
    }
}
