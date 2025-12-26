// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Text;
using Oxygen.Assets.Catalog.FileSystem;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Assets.Persistence.Pak.V1;
using Oxygen.Storage;

namespace Oxygen.Assets.Catalog.Pak;

/// <summary>
/// A catalog provider backed by a PAK file asset directory (v1).
/// </summary>
/// <remarks>
/// This provider enumerates assets via an embedded browse index and maps virtual paths
/// (e.g. <c>/Content/Textures/Wood.png</c>) to canonical asset URIs
/// (e.g. <c>asset:///Content/Textures/Wood.png</c>).
/// </remarks>
#pragma warning disable SA1204 // Static elements should appear before instance elements
public sealed class PakIndexAssetCatalog : IAssetCatalog, IDisposable
{
    private const int PakHeaderSize = 64;
    private const int PakFooterSize = 256;
    private const int AssetDirectoryEntrySize = 64;

    private static readonly byte[] HeaderMagic = Encoding.ASCII.GetBytes("OXPAK\0\0\0");
    private static readonly byte[] FooterMagic = Encoding.ASCII.GetBytes("OXPAKEND");
    private static readonly byte[] BrowseIndexMagic = Encoding.ASCII.GetBytes("OXPAKBIX");

    private readonly IStorageProvider storage;
    private readonly PakIndexAssetCatalogOptions options;
    private readonly IFileSystemCatalogEventSource eventSource;

    private readonly Subject<AssetChange> changes = new();
    private readonly ConcurrentDictionary<Uri, AssetKey> keysByUri = new();
    private readonly IDisposable eventSubscription;

    private volatile bool isInitialized;
    private string pakPath = string.Empty;

    /// <summary>
    /// Initializes a new instance of the <see cref="PakIndexAssetCatalog"/> class.
    /// </summary>
    /// <param name="storage">The storage provider used to read the pak file.</param>
    /// <param name="options">Provider options.</param>
    public PakIndexAssetCatalog(IStorageProvider storage, PakIndexAssetCatalogOptions options)
        : this(storage, options, eventSource: null)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="PakIndexAssetCatalog"/> class using an injected event source.
    /// </summary>
    /// <param name="storage">The storage provider used to read the pak file.</param>
    /// <param name="options">Provider options.</param>
    /// <param name="eventSource">Optional injected filesystem event source (tests).</param>
    internal PakIndexAssetCatalog(
        IStorageProvider storage,
        PakIndexAssetCatalogOptions options,
        IFileSystemCatalogEventSource? eventSource)
    {
        ArgumentNullException.ThrowIfNull(storage);
        ArgumentNullException.ThrowIfNull(options);
        if (string.IsNullOrEmpty(options.MountPoint))
        {
            throw new ArgumentException("MountPoint must not be null or empty.", nameof(options));
        }

        if (string.IsNullOrEmpty(options.PakFilePath))
        {
            throw new ArgumentException("PakFilePath must not be null or empty.", nameof(options));
        }

        this.storage = storage;
        this.options = options;

        this.pakPath = this.storage.Normalize(options.PakFilePath);

        if (eventSource is not null)
        {
            this.eventSource = eventSource;
            this.eventSubscription = this.eventSource.Events
                .Buffer(TimeSpan.FromMilliseconds(100))
                .Where(batch => batch.Count > 0)
                .Subscribe(batch => _ = this.ReloadAndDiffAsync());
        }
        else
        {
            var parent = Path.GetDirectoryName(this.pakPath);
            var filter = options.WatcherFilter ?? Path.GetFileName(this.pakPath);

            if (!string.IsNullOrEmpty(parent) && Directory.Exists(parent))
            {
                this.eventSource = new FileSystemWatcherEventSource(rootPath: parent, filter: filter);
                this.eventSubscription = this.eventSource.Events
                    .Buffer(TimeSpan.FromMilliseconds(100))
                    .Where(batch => batch.Count > 0)
                    .Subscribe(batch => _ = this.ReloadAndDiffAsync());
            }
            else
            {
                this.eventSource = new NoopFileSystemCatalogEventSource();
                this.eventSubscription = Disposable.Empty;
            }
        }
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.AsObservable();

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        await this.EnsureInitializedAsync(cancellationToken).ConfigureAwait(false);

        IEnumerable<Uri> uris = this.keysByUri.Keys;
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

        var mountPoint = trimmed[..slash];
        var path = trimmed[(slash + 1)..];
        return AssetUriHelper.CreateUri(mountPoint, path);
    }

    private async Task EnsureInitializedAsync(CancellationToken cancellationToken)
    {
        if (this.isInitialized)
        {
            return;
        }

        await this.ReloadSnapshotAsync(cancellationToken).ConfigureAwait(false);
        this.isInitialized = true;
    }

    private async Task ReloadAndDiffAsync()
    {
        try
        {
            await this.EnsureInitializedAsync(CancellationToken.None).ConfigureAwait(false);

            var before = this.keysByUri.ToDictionary(kvp => kvp.Key, kvp => kvp.Value);
            await this.ReloadSnapshotAsync(CancellationToken.None).ConfigureAwait(false);
            var after = this.keysByUri.ToDictionary(kvp => kvp.Key, kvp => kvp.Value);

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
            var beforeUris = this.keysByUri.Keys.ToArray();
            this.keysByUri.Clear();
            foreach (var uri in beforeUris)
            {
                this.changes.OnNext(new AssetChange(AssetChangeKind.Removed, uri));
            }

            _ = ex;
        }
    }

    private async Task ReloadSnapshotAsync(CancellationToken cancellationToken)
    {
        var doc = await this.storage.GetDocumentFromPathAsync(this.pakPath, cancellationToken).ConfigureAwait(false);
        if (!await doc.ExistsAsync().ConfigureAwait(false))
        {
            this.keysByUri.Clear();
            return;
        }

        using var stream = await doc.OpenReadAsync(cancellationToken).ConfigureAwait(false);

        var browse = ReadV1BrowseIndex(stream);
        var next = new Dictionary<Uri, AssetKey>();
        foreach (var entry in browse.Entries)
        {
            next[VirtualPathToAssetUri(entry.VirtualPath)] = entry.AssetKey;
        }

        this.keysByUri.Clear();
        foreach (var kvp in next)
        {
            _ = this.keysByUri.TryAdd(kvp.Key, kvp.Value);
        }
    }

    private static PakBrowseIndex ReadV1BrowseIndex(Stream stream)
    {
        var (offset, size) = FindBrowseIndexLocation(stream);
        if (offset == 0 || size == 0)
        {
            throw new InvalidDataException("Pak browse index is missing (required for virtual-path enumeration).");
        }

        if (size > int.MaxValue)
        {
            throw new InvalidDataException("Pak browse index is too large.");
        }

        stream.Seek((long)offset, SeekOrigin.Begin);
        var bytes = new byte[(int)size];
        ReadExactly(stream, bytes);
        using var ms = new MemoryStream(bytes, writable: false);
        return PakBrowseIndex.Read(ms);
    }

    private static (ulong offset, ulong size) FindBrowseIndexLocation(Stream stream)
    {
        if (stream.Length < PakHeaderSize + PakFooterSize)
        {
            throw new InvalidDataException("Pak file is too small.");
        }

        // Read footer.
        stream.Seek(-PakFooterSize, SeekOrigin.End);
        Span<byte> footer = stackalloc byte[PakFooterSize];
        ReadExactly(stream, footer);

        if (!footer.Slice(PakFooterSize - FooterMagic.Length, FooterMagic.Length).SequenceEqual(FooterMagic))
        {
            throw new InvalidDataException("Pak footer magic is invalid.");
        }

        // Two layouts exist in the repo today:
        // - C++ struct layout: reserved[124] comes before pak_crc32
        // - PakGen writer layout: pak_crc32 comes before reserved[124]
        // We store browse index metadata (offset,size) in the reserved bytes, so try both.
        var candidateA = ReadBrowseIndexMetaFromFooter(footer, reservedStartOffsetInFooter: 120);
        var candidateB = ReadBrowseIndexMetaFromFooter(footer, reservedStartOffsetInFooter: 124);

        if (IsValidCandidate(stream, candidateA.offset, candidateA.size) && IsBrowseIndexAt(stream, candidateA.offset))
        {
            return candidateA;
        }

        if (IsValidCandidate(stream, candidateB.offset, candidateB.size) && IsBrowseIndexAt(stream, candidateB.offset))
        {
            return candidateB;
        }

        if (IsValidCandidate(stream, candidateA.offset, candidateA.size))
        {
            return candidateA;
        }

        if (IsValidCandidate(stream, candidateB.offset, candidateB.size))
        {
            return candidateB;
        }

        return default;
    }

    private static (ulong offset, ulong size) ReadBrowseIndexMetaFromFooter(ReadOnlySpan<byte> footer, int reservedStartOffsetInFooter)
    {
        if (reservedStartOffsetInFooter < 0 || reservedStartOffsetInFooter + 16 > footer.Length)
        {
            return default;
        }

        var offset = BinaryPrimitives.ReadUInt64LittleEndian(footer.Slice(reservedStartOffsetInFooter, 8));
        var size = BinaryPrimitives.ReadUInt64LittleEndian(footer.Slice(reservedStartOffsetInFooter + 8, 8));
        return (offset, size);
    }

    private static bool IsValidCandidate(Stream stream, ulong offset, ulong size)
    {
        if (offset == 0 || size == 0)
        {
            return false;
        }

        if (offset > (ulong)stream.Length)
        {
            return false;
        }

        return offset + size <= (ulong)stream.Length;
    }

    private static bool IsBrowseIndexAt(Stream stream, ulong offset)
    {
        var saved = stream.Position;
        try
        {
            stream.Seek((long)offset, SeekOrigin.Begin);
            Span<byte> magic = stackalloc byte[8];
            ReadExactly(stream, magic);
            return magic.SequenceEqual(BrowseIndexMagic);
        }
        catch (Exception ex) when (ex is IOException or EndOfStreamException)
        {
            _ = ex;
            return false;
        }
        finally
        {
            stream.Seek(saved, SeekOrigin.Begin);
        }
    }

    private static AssetKey[] ReadV1DirectoryKeys(Stream stream)
    {
        if (stream.Length < PakHeaderSize + PakFooterSize)
        {
            throw new InvalidDataException("Pak file is too small.");
        }

        Span<byte> header = stackalloc byte[PakHeaderSize];
        ReadExactly(stream, header);

        if (!header[..HeaderMagic.Length].SequenceEqual(HeaderMagic))
        {
            throw new InvalidDataException("Pak header magic is invalid.");
        }

        stream.Seek(-PakFooterSize, SeekOrigin.End);
        Span<byte> footer = stackalloc byte[PakFooterSize];
        ReadExactly(stream, footer);

        if (!footer.Slice(PakFooterSize - FooterMagic.Length, FooterMagic.Length).SequenceEqual(FooterMagic))
        {
            throw new InvalidDataException("Pak footer magic is invalid.");
        }

        var directoryOffset = BinaryPrimitives.ReadUInt64LittleEndian(footer[..8]);
        var directorySize = BinaryPrimitives.ReadUInt64LittleEndian(footer.Slice(8, 8));
        var assetCount = BinaryPrimitives.ReadUInt64LittleEndian(footer.Slice(16, 8));

        if (assetCount > int.MaxValue)
        {
            throw new InvalidDataException("Pak asset count is too large.");
        }

        var expectedSize = checked((ulong)AssetDirectoryEntrySize * assetCount);
        if (directorySize < expectedSize)
        {
            throw new InvalidDataException("Pak directory size is invalid.");
        }

        if (directoryOffset > (ulong)stream.Length)
        {
            throw new InvalidDataException("Pak directory offset is out of bounds.");
        }

        stream.Seek((long)directoryOffset, SeekOrigin.Begin);

        var keys = new AssetKey[(int)assetCount];
        Span<byte> entry = stackalloc byte[AssetDirectoryEntrySize];
        for (var i = 0; i < keys.Length; i++)
        {
            ReadExactly(stream, entry);
            keys[i] = AssetKey.FromBytes(entry[..16]);
        }

        return keys;
    }

    private static void ReadExactly(Stream stream, Span<byte> buffer)
    {
        var totalRead = 0;
        while (totalRead < buffer.Length)
        {
            var read = stream.Read(buffer[totalRead..]);
            if (read == 0)
            {
                throw new EndOfStreamException("Unexpected end of stream.");
            }

            totalRead += read;
        }
    }

    private static void ReadExactly(Stream stream, byte[] buffer)
        => ReadExactly(stream, buffer.AsSpan());

    private sealed class NoopFileSystemCatalogEventSource : IFileSystemCatalogEventSource
    {
        public IObservable<FileSystemCatalogEvent> Events { get; } = Observable.Empty<FileSystemCatalogEvent>();

        public void Dispose()
        {
        }
    }
}
#pragma warning restore SA1204 // Static elements should appear before instance elements
