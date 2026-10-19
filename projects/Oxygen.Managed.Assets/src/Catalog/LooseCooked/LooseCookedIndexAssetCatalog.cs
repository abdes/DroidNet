// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Storage;
using Oxygen.Managed.Assets.Catalog.FileSystem;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog.LooseCooked;

/// <summary>
/// A catalog provider backed by a runtime-compatible loose cooked index (<c>container.index.bin</c>).
/// </summary>
/// <remarks>
/// This provider enumerates the keyed assets and the texture descriptors of the current index, identified by the
/// native virtual paths it records (e.g. <c>/Content/Materials/Wood.omat</c>); a texture's record carries the facts its
/// descriptor file states.
/// </remarks>
public sealed class LooseCookedIndexAssetCatalog : IAssetCatalog, IRefreshableAssetCatalog, IDisposable
{
    private readonly IStorageProvider? storage;
    private readonly LooseCookedIndexAssetCatalogOptions? options;
    private readonly IFileSystemCatalogEventSource eventSource;
    private readonly Document? capturedDocument;

    private readonly Subject<AssetChange> changes = new();
    private readonly IDisposable eventSubscription;

    private IReadOnlyDictionary<AssetPath, AssetRecord> entries = new Dictionary<AssetPath, AssetRecord>();
    private volatile bool isInitialized;
    private readonly FileSystemPath cookedRoot;

    /// <summary>Creates a fixed catalog from an already captured index; it never watches or reloads its source.</summary>
    /// <param name="document">The decoded index accepted by its content owner.</param>
    /// <param name="cookedRoot">The physical root named by that index.</param>
    public LooseCookedIndexAssetCatalog(Document document, FileSystemPath cookedRoot)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(cookedRoot);
        this.cookedRoot = cookedRoot;
        this.capturedDocument = document;
        this.eventSource = new NoopFileSystemCatalogEventSource();
        this.eventSubscription = Disposable.Empty;
    }

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

        ArgumentNullException.ThrowIfNull(options.CookedRootFolderPath);
        if (string.IsNullOrEmpty(options.IndexFileName))
        {
            throw new ArgumentException("IndexFileName must not be null or empty.", nameof(options));
        }

        this.storage = storage;
        this.options = options;
        this.cookedRoot = options.CookedRootFolderPath;

        var filter = options.WatcherFilter ?? options.IndexFileName;

        // NOTE:
        // A configured cooked root may not exist until its first publication.
        // That folder (or even the index file) may not exist yet when this catalog is constructed.
        // If we only watch when the exact root exists, we can permanently miss later creation of
        // the cooked root's "container.index.bin". To avoid that, watch the nearest existing
        // parent directory (usually the ".cooked" folder) with IncludeSubdirectories enabled.
        var watchRoot = FindNearestExistingDirectory(this.cookedRoot.FullPath);

        if (eventSource is not null)
        {
            this.eventSource = eventSource;
            this.eventSubscription = this.eventSource.Events
                .Buffer(TimeSpan.FromMilliseconds(100))
                .Where(batch => batch.Count > 0)
                .Subscribe(batch => _ = this.ReloadAndDiffAsync());
        }
        else if (!string.IsNullOrEmpty(watchRoot))
        {
            this.eventSource = new FileSystemWatcherEventSource(rootPath: watchRoot, filter: filter);
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

        return AssetQueryMatcher.Apply(query, Volatile.Read(ref this.entries).Values);
    }

    /// <inheritdoc />
    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);
        await this.ReloadSnapshotAsync(cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.eventSubscription.Dispose();
        this.eventSource.Dispose();
        this.changes.OnCompleted();
        this.changes.Dispose();
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

    private static async Task<byte[]?> ReadDiskFileAsync(string path, CancellationToken cancellationToken)
    {
        try
        {
            return await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static async Task<IReadOnlyDictionary<AssetPath, AssetRecord>> CreateEntriesAsync(
        Document document,
        FileSystemPath root,
        Func<string, CancellationToken, Task<byte[]?>> readFile,
        CancellationToken cancellationToken)
    {
        var records = new CookedAssetRecords();
        foreach (var entry in document.Assets)
        {
            records.Add(entry.VirtualPath ?? string.Empty, identity => new(root, entry.DescriptorRelativePath, document.SourceGuid, entry.AssetKey, entry.AssetType, entry.DescriptorSize, Convert.ToHexString(entry.DescriptorSha256.Span))
            {
                VirtualPath = identity,
                References = entry.References,
            });
        }

        foreach (var file in document.Files.Where(static file => file.Kind == FileKind.TextureDescriptor && file.VirtualPath is not null))
        {
            var texture = ReadTexture(await readFile(Path.Combine(root.FullPath, file.RelativePath), cancellationToken).ConfigureAwait(false));
            records.Add(file.VirtualPath!, identity => new(root, file.RelativePath, document.SourceGuid, AssetKey.FromVirtualPath(file.VirtualPath!), (byte)AssetType.Unknown, file.Size, Convert.ToHexString(file.Sha256.Span))
            {
                VirtualPath = identity,
                Texture = texture,
            });
        }

        return records.Records;
    }

    // A texture whose descriptor cannot be read is still listed, without its facts; validation reports the damage.
    private static TextureResourceRecord? ReadTexture(byte[]? bytes)
    {
        try
        {
            return bytes is null ? null : TextureDescriptorFile.Read(bytes);
        }
        catch (InvalidDataException)
        {
            return null;
        }
    }

    private async Task<byte[]?> ReadStorageFileAsync(string path, CancellationToken cancellationToken)
    {
        var document = await this.storage!.GetDocumentFromPathAsync(path, cancellationToken).ConfigureAwait(false);
        if (!await document.ExistsAsync().ConfigureAwait(false))
        {
            return null;
        }

        var stream = await document.OpenReadAsync(cancellationToken).ConfigureAwait(false);
        await using (stream.ConfigureAwait(false))
        {
            using var copy = new MemoryStream();
            await stream.CopyToAsync(copy, cancellationToken).ConfigureAwait(false);
            return copy.ToArray();
        }
    }

    private async Task EnsureInitializedAsync(CancellationToken cancellationToken)
    {
        if (this.isInitialized)
        {
            return;
        }

        if (this.capturedDocument is { } document)
        {
            Volatile.Write(ref this.entries, await CreateEntriesAsync(document, this.cookedRoot, ReadDiskFileAsync, cancellationToken).ConfigureAwait(false));
        }
        else
        {
            await this.ReloadSnapshotAsync(cancellationToken).ConfigureAwait(false);
        }

        this.isInitialized = true;
    }

    private async Task ReloadAndDiffAsync()
    {
        try
        {
            await this.EnsureInitializedAsync(CancellationToken.None).ConfigureAwait(false);

            var before = Volatile.Read(ref this.entries);
            await this.ReloadSnapshotAsync(CancellationToken.None).ConfigureAwait(false);
            var after = Volatile.Read(ref this.entries);

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
            var beforeIdentities = Interlocked.Exchange(ref this.entries, new Dictionary<AssetPath, AssetRecord>()).Keys;
            foreach (var identity in beforeIdentities)
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, identity));
            }

            _ = ex; // silence analyzer about unused filtered exception variable
        }
    }

    private async Task ReloadSnapshotAsync(CancellationToken cancellationToken)
    {
        if (this.storage is null || this.options is null)
        {
            return;
        }

        var indexPath = this.storage.NormalizeRelativeTo(this.cookedRoot.FullPath, this.options.IndexFileName);
        var indexDoc = await this.storage.GetDocumentFromPathAsync(indexPath, cancellationToken).ConfigureAwait(false);

        if (!await indexDoc.ExistsAsync().ConfigureAwait(false))
        {
            Volatile.Write(ref this.entries, new Dictionary<AssetPath, AssetRecord>());
            return;
        }

        var stream = await indexDoc.OpenReadAsync(cancellationToken).ConfigureAwait(false);
        await using var streamLifetime = stream.ConfigureAwait(false);
        var document = LooseCookedIndex.Read(stream);
        Volatile.Write(ref this.entries, await CreateEntriesAsync(document, this.cookedRoot, this.ReadStorageFileAsync, cancellationToken).ConfigureAwait(false));
    }

    private sealed class NoopFileSystemCatalogEventSource : IFileSystemCatalogEventSource
    {
        public IObservable<FileSystemCatalogEvent> Events { get; } = Observable.Empty<FileSystemCatalogEvent>();

        public void Dispose()
        {
        }
    }
}
