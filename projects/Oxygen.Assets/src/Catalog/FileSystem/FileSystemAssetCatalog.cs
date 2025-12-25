// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Storage;

namespace Oxygen.Assets.Catalog.FileSystem;

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
public sealed class FileSystemAssetCatalog : IAssetCatalog, IDisposable
{
    private readonly IStorageProvider storage;
    private readonly FileSystemAssetCatalogOptions options;
    private readonly IFileSystemCatalogEventSource eventSource;

    private readonly Subject<AssetChange> changes = new();
    private readonly ConcurrentDictionary<Uri, AssetRecord> records = new();
    private readonly IDisposable eventSubscription;

    private volatile bool isInitialized;
    private IFolder? rootFolder;

    /// <summary>
    /// Initializes a new instance of the <see cref="FileSystemAssetCatalog"/> class.
    /// </summary>
    /// <param name="storage">The storage provider used for snapshot enumeration.</param>
    /// <param name="options">Provider options.</param>
    public FileSystemAssetCatalog(IStorageProvider storage, FileSystemAssetCatalogOptions options)
        : this(storage, options, eventSource: null)
    {
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "MA0015:Specify the parameter name in ArgumentException", Justification = "valid use")]
    internal FileSystemAssetCatalog(
        IStorageProvider storage,
        FileSystemAssetCatalogOptions options,
        IFileSystemCatalogEventSource? eventSource)
    {
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentNullException.ThrowIfNull(options);
        ArgumentException.ThrowIfNullOrEmpty(options.MountPoint);
        ArgumentException.ThrowIfNullOrEmpty(options.RootFolderPath);

        this.storage = storage;
        this.options = options;

        // Watching is optional in tests via injected source.
        this.eventSource = eventSource
            ?? new FileSystemWatcherEventSource(
                rootPath: this.storage.Normalize(options.RootFolderPath),
                filter: options.WatcherFilter);

        // Coalesce bursts to avoid watcher noise (rename often appears as many events).
        this.eventSubscription = this.eventSource.Events
            .Buffer(TimeSpan.FromMilliseconds(100))
            .Where(batch => batch.Count > 0)
            .Subscribe(batch => _ = this.ApplyBatchAsync(batch.AsReadOnly()));
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

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

    public void Dispose()
    {
        this.eventSubscription.Dispose();
        this.eventSource.Dispose();
        this.changes.OnCompleted();
        this.changes.Dispose();
    }

    private static bool MatchesSearch(AssetRecord record, string term)
        => record.Name.Contains(term, StringComparison.OrdinalIgnoreCase)
            || record.Uri.ToString().Contains(term, StringComparison.OrdinalIgnoreCase)
            || AssetUriHelper.GetMountPoint(record.Uri).Contains(term, StringComparison.OrdinalIgnoreCase);

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

            if (segment.Length > 0 && segment[0] == '.' && !IsAllowedDotName(segment))
            {
                return false;
            }
        }

        return true;
    }

    private static bool IsAllowedDotName(string name)
        => false;

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
        foreach (var record in snapshot)
        {
            _ = this.records.TryAdd(record.Uri, record);
        }

        this.isInitialized = true;
    }

    private async Task<IReadOnlyList<AssetRecord>> EnumerateAllFilesAsync(IFolder root, CancellationToken cancellationToken)
    {
        var results = new List<AssetRecord>();
        var pending = new Stack<IFolder>();
        pending.Push(root);

        while (pending.Count > 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var folder = pending.Pop();

            await foreach (var document in folder.GetDocumentsAsync(cancellationToken).ConfigureAwait(false))
            {
                var relative = Path.GetRelativePath(root.Location, document.Location);
                if (ShouldInclude(relative))
                {
                    var uri = this.MapFilePathToAssetUri(root.Location, document.Location);
                    results.Add(new AssetRecord(uri));
                }
            }

            await foreach (var childFolder in folder.GetFoldersAsync(cancellationToken).ConfigureAwait(false))
            {
                var relative = Path.GetRelativePath(root.Location, childFolder.Location);
                if (ShouldInclude(relative))
                {
                    pending.Push(childFolder);
                }
            }
        }

        return results;
    }

    private Uri MapFilePathToAssetUri(string rootLocation, string fullPath)
    {
        var relative = Path.GetRelativePath(rootLocation, fullPath);
        return AssetUriHelper.CreateUri(this.options.MountPoint, relative);
    }

    private async Task ApplyBatchAsync(IReadOnlyList<FileSystemCatalogEvent> batch)
    {
        try
        {
            // Ensure we have an initial baseline before applying deltas.
            await this.EnsureInitializedAsync(CancellationToken.None).ConfigureAwait(false);
            if (this.rootFolder is null)
            {
                return;
            }

            foreach (var ev in batch)
            {
                switch (ev.Kind)
                {
                    case FileSystemCatalogEventKind.RescanRequired:
                        await this.RescanAsync().ConfigureAwait(false);
                        break;

                    case FileSystemCatalogEventKind.Renamed:
                        this.ApplyRenamed(ev);
                        break;

                    case FileSystemCatalogEventKind.Created:
                        this.ApplyCreated(ev);
                        break;

                    case FileSystemCatalogEventKind.Changed:
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
            await this.RescanAsync().ConfigureAwait(false);
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
            this.changes.OnNext(new AssetChange(AssetChangeKind.Added, uri));
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
        this.changes.OnNext(new AssetChange(isNew ? AssetChangeKind.Added : AssetChangeKind.Updated, uri));
    }

    private void ApplyDeleted(FileSystemCatalogEvent ev)
    {
        if (!this.TryMapIfUnderRoot(ev.FullPath, out var uri))
        {
            return;
        }

        if (this.records.TryRemove(uri, out _))
        {
            this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, uri));
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
            // Old was outside scope; new might be inside.
            this.ApplyCreated(ev);
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
            this.changes.OnNext(new AssetChange(AssetChangeKind.Relocated, newUri, PreviousUri: oldUri));
        }
        else
        {
            // If we didn't know the old key, treat as add.
            this.ApplyCreated(ev);
        }
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
        if (!ShouldInclude(relative))
        {
            return false;
        }

        uri = this.MapFilePathToAssetUri(root, fullPath);
        return true;
    }

    private async Task RescanAsync()
    {
        if (this.rootFolder is null)
        {
            return;
        }

        var snapshot = await this.EnumerateAllFilesAsync(this.rootFolder, CancellationToken.None).ConfigureAwait(false);
        var next = snapshot.ToDictionary(r => r.Uri, r => r);

        // Removed
        foreach (var existing in this.records.Keys)
        {
            if (!next.ContainsKey(existing) && this.records.TryRemove(existing, out _))
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, existing));
            }
        }

        // Added
        foreach (var kvp in next)
        {
            if (this.records.TryAdd(kvp.Key, kvp.Value))
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Added, kvp.Key));
            }
        }
    }
}
