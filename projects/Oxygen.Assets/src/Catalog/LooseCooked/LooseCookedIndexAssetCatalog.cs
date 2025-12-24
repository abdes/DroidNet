// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using Oxygen.Assets.Catalog.FileSystem;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Storage;

namespace Oxygen.Assets.Catalog.LooseCooked;

/// <summary>
/// A catalog provider backed by a runtime-compatible loose cooked index (<c>container.index.bin</c>).
/// </summary>
/// <remarks>
/// This provider treats the v1 index as the source of truth for cooked asset enumeration.
/// It maps v1 <c>VirtualPath</c> values (e.g. <c>/Content/Textures/Wood.png</c>) to canonical
/// asset URIs (e.g. <c>asset:///Content/Textures/Wood.png</c>).
/// </remarks>
public sealed class LooseCookedIndexAssetCatalog : IAssetCatalog, IDisposable
{
    private readonly IStorageProvider storage;
    private readonly LooseCookedIndexAssetCatalogOptions options;
    private readonly IFileSystemCatalogEventSource eventSource;

    private readonly Subject<AssetChange> changes = new();
    private readonly ConcurrentDictionary<Uri, AssetEntry> entriesByUri = new();
    private readonly IDisposable eventSubscription;

    private volatile bool isInitialized;
    private string cookedRoot = string.Empty;

    /// <summary>
    /// Initializes a new instance of the <see cref="LooseCookedIndexAssetCatalog"/> class.
    /// </summary>
    /// <param name="storage">The storage provider used to resolve the cooked root and read the index file.</param>
    /// <param name="options">Provider options.</param>
    public LooseCookedIndexAssetCatalog(IStorageProvider storage, LooseCookedIndexAssetCatalogOptions options)
        : this(storage, options, eventSource: null)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="LooseCookedIndexAssetCatalog"/> class using an injected event source.
    /// </summary>
    /// <param name="storage">The storage provider used to resolve the cooked root and read the index file.</param>
    /// <param name="options">Provider options.</param>
    /// <param name="eventSource">Optional injected filesystem event source (tests).</param>
    internal LooseCookedIndexAssetCatalog(
        IStorageProvider storage,
        LooseCookedIndexAssetCatalogOptions options,
        IFileSystemCatalogEventSource? eventSource)
    {
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentNullException.ThrowIfNull(options);

        if (string.IsNullOrEmpty(options.CookedRootFolderPath))
        {
            throw new ArgumentException("CookedRootFolderPath must not be null or empty.", nameof(options));
        }

        if (string.IsNullOrEmpty(options.IndexFileName))
        {
            throw new ArgumentException("IndexFileName must not be null or empty.", nameof(options));
        }

        this.storage = storage;
        this.options = options;

        var normalizedRoot = this.storage.Normalize(options.CookedRootFolderPath);
        var filter = options.WatcherFilter ?? options.IndexFileName;

        if (eventSource is not null)
        {
            this.eventSource = eventSource;
            this.eventSubscription = this.eventSource.Events
                .Buffer(TimeSpan.FromMilliseconds(100))
                .Where(batch => batch.Count > 0)
                .Subscribe(batch => _ = this.ReloadAndDiffAsync());
        }
        else if (Directory.Exists(normalizedRoot))
        {
            this.eventSource = new FileSystemWatcherEventSource(rootPath: normalizedRoot, filter: filter);
            this.eventSubscription = this.eventSource.Events
                .Buffer(TimeSpan.FromMilliseconds(100))
                .Where(batch => batch.Count > 0)
                .Subscribe(batch => _ = this.ReloadAndDiffAsync());
        }
        else
        {
            // The BCL FileSystemWatcher requires a real on-disk directory.
            // When the cooked root doesn't exist (e.g. tests using a mock filesystem), disable watching.
            this.eventSource = new NoopFileSystemCatalogEventSource();
            this.eventSubscription = Disposable.Empty;
        }
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);

        IEnumerable<Uri> uris = this.entriesByUri.Keys;
        uris = uris.Where(uri => AssetQueryScopeMatcher.IsMatch(query.Scope, uri));

        if (!string.IsNullOrWhiteSpace(query.SearchText))
        {
            var term = query.SearchText.Trim();
            uris = uris.Where(uri => uri.ToString().Contains(term, StringComparison.OrdinalIgnoreCase));
        }

        return uris
            .OrderBy(u => u.ToString(), StringComparer.Ordinal)
            .Select(u => new AssetRecord(u))
            .ToArray();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.eventSubscription.Dispose();
        this.eventSource.Dispose();
        this.changes.OnCompleted();
        this.changes.Dispose();
    }

    private static Uri VirtualPathToAssetUri(string virtualPath)
    {
        if (string.IsNullOrEmpty(virtualPath) || virtualPath[0] != '/')
        {
            throw new InvalidDataException("VirtualPath must start with '/'.");
        }

        var trimmed = virtualPath.TrimStart('/');
        var slash = trimmed.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0 || slash == trimmed.Length - 1)
        {
            throw new InvalidDataException("VirtualPath must be of the form '/{Mount}/{Path}'.");
        }

        var authority = trimmed[..slash];
        var path = trimmed[(slash + 1)..];
        return new Uri($"asset:///{authority}/{path}");
    }

    private async Task EnsureInitializedAsync(CancellationToken cancellationToken)
    {
        if (this.isInitialized)
        {
            return;
        }

        this.cookedRoot = this.storage.Normalize(this.options.CookedRootFolderPath);
        await this.ReloadSnapshotAsync(cancellationToken).ConfigureAwait(false);
        this.isInitialized = true;
    }

    private async Task ReloadAndDiffAsync()
    {
        try
        {
            await this.EnsureInitializedAsync(CancellationToken.None).ConfigureAwait(false);

            var before = this.entriesByUri.ToDictionary(kvp => kvp.Key, kvp => kvp.Value);
            await this.ReloadSnapshotAsync(CancellationToken.None).ConfigureAwait(false);
            var after = this.entriesByUri.ToDictionary(kvp => kvp.Key, kvp => kvp.Value);

            foreach (var removed in before.Keys.Except(after.Keys))
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, removed));
            }

            foreach (var added in after.Keys.Except(before.Keys))
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Added, added));
            }

            foreach (var common in before.Keys.Intersect(after.Keys))
            {
                if (!Equals(before[common], after[common]))
                {
                    this.changes.OnNext(new AssetChange(AssetChangeKind.Updated, common));
                }
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            // If anything goes wrong while reloading/diffing, fall back to clearing.
            var beforeUris = this.entriesByUri.Keys.ToArray();
            this.entriesByUri.Clear();
            foreach (var uri in beforeUris)
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, uri));
            }

            _ = ex; // silence analyzer about unused filtered exception variable
        }
    }

    private async Task ReloadSnapshotAsync(CancellationToken cancellationToken)
    {
        var indexPath = this.storage.NormalizeRelativeTo(this.cookedRoot, this.options.IndexFileName);
        var indexDoc = await this.storage.GetDocumentFromPathAsync(indexPath, cancellationToken).ConfigureAwait(false);

        if (!await indexDoc.ExistsAsync().ConfigureAwait(false))
        {
            this.entriesByUri.Clear();
            return;
        }

        using var stream = await indexDoc.OpenReadAsync(cancellationToken).ConfigureAwait(false);
        var document = LooseCookedIndex.Read(stream);

        var next = new Dictionary<Uri, AssetEntry>();
        foreach (var entry in document.Assets)
        {
            if (string.IsNullOrWhiteSpace(entry.VirtualPath))
            {
                continue;
            }

            var uri = VirtualPathToAssetUri(entry.VirtualPath);
            next[uri] = entry;
        }

        this.entriesByUri.Clear();
        foreach (var kvp in next)
        {
            _ = this.entriesByUri.TryAdd(kvp.Key, kvp.Value);
        }
    }

    private sealed class NoopFileSystemCatalogEventSource : IFileSystemCatalogEventSource
    {
        public IObservable<FileSystemCatalogEvent> Events { get; } = Observable.Empty<FileSystemCatalogEvent>();

        public void Dispose()
        {
        }
    }
}
