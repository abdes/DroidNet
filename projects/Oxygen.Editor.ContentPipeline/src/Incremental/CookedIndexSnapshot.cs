// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Security.Cryptography;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Index identity and catalog metadata, without certifying payload integrity.</summary>
/// <remarks>
/// Every read hashes the index bytes, so its fingerprint is always that of the file read; the parse, its catalog records
/// and the finding that the root needs recooking are shared by every read of the same bytes at the same path.
/// </remarks>
internal sealed class CookedIndexSnapshot
{
    // The latest read of each index path, by the digest of the bytes read; other bytes replace it.
    private static readonly ConcurrentDictionary<string, (string Fingerprint, CookedIndexRead Read)> Reads
        = new(StringComparer.OrdinalIgnoreCase);

    private readonly Lazy<Task<IReadOnlyList<AssetRecord>>> records;

    private CookedIndexSnapshot(Document index, string fingerprint, FileSystemPath root)
    {
        this.Index = index;
        this.Fingerprint = fingerprint;
        this.records = new(() => BuildRecordsAsync(index, root));
    }

    /// <summary>Gets the index metadata.</summary>
    public Document Index { get; }

    /// <summary>Gets the SHA-256 of the index bytes, which identifies the root's content.</summary>
    public string Fingerprint { get; }

    /// <summary>Reads one immutable index opening, reusing the parse of identical bytes. No content payload is read.</summary>
    /// <param name="root">The loose cooked root.</param>
    /// <param name="cancellationToken">Cancels reading.</param>
    /// <returns>The current index, or the format of an index that needs recooking.</returns>
    /// <exception cref="FileNotFoundException">The root has no index.</exception>
    /// <exception cref="InvalidDataException">The index is damaged.</exception>
    public static async Task<CookedIndexRead> ReadAsync(string root, CancellationToken cancellationToken)
    {
        var path = Path.GetFullPath(Path.Combine(root, "container.index.bin"));
        var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
        await using var lifetime = file.ConfigureAwait(false);
        using var bytes = new MemoryStream();
        await file.CopyToAsync(bytes, cancellationToken).ConfigureAwait(false);
        var content = bytes.GetBuffer().AsSpan(0, checked((int)bytes.Length));
        var fingerprint = Convert.ToHexString(SHA256.HashData(content));
        if (Reads.TryGetValue(path, out var cached) && string.Equals(cached.Fingerprint, fingerprint, StringComparison.Ordinal))
        {
            return cached.Read;
        }

        CookedIndexRead read = LooseCookedIndex.ReadFormatVersion(content) is { } version && version != LooseCookedIndex.Version
            ? new CookedIndexRead.Stale(version)
            : new CookedIndexRead.Current(new(LooseCookedIndex.Read(Rewind(bytes)), fingerprint, FileSystemPath.From(root)));
        Reads[path] = (fingerprint, read);
        return read;
    }

    /// <summary>Gets the catalog records of the root's assets and textures, built once per index opening.</summary>
    /// <param name="cancellationToken">Cancels waiting; the shared read continues for other callers.</param>
    /// <returns>The records.</returns>
    public Task<IReadOnlyList<AssetRecord>> ReadRecordsAsync(CancellationToken cancellationToken)
        => this.records.Value.WaitAsync(cancellationToken);

    /// <summary>Checks the protected member list and sizes, without hashing their contents.</summary>
    public void ValidateMetadata(IReadOnlyList<CookedFileEntry> actual)
    {
        var expected = this.Index.Assets.Select(static asset => (Path: asset.DescriptorRelativePath, Size: asset.DescriptorSize))
            .Concat(this.Index.Files.Select(static file => (Path: file.RelativePath, file.Size)))
            .ToDictionary(static file => file.Path, static file => file.Size, StringComparer.Ordinal);
        foreach (var file in actual.Where(static file => file.RelativePath is not ("container.index.bin" or ".generation.lock")))
        {
            if (!expected.Remove(file.RelativePath, out var size) || file.Size != size)
            {
                throw new InvalidDataException($"Cooked content metadata does not match its index: {file.RelativePath}.");
            }
        }

        if (expected.Count != 0)
        {
            throw new InvalidDataException($"Cooked content is missing an indexed file: {expected.First().Key}.");
        }
    }

    private static async Task<IReadOnlyList<AssetRecord>> BuildRecordsAsync(Document index, FileSystemPath root)
    {
        var catalog = new LooseCookedIndexAssetCatalog(index, root);
        return await catalog.QueryAsync(new(AssetQueryScope.All), CancellationToken.None).ConfigureAwait(false);
    }

    private static MemoryStream Rewind(MemoryStream stream)
    {
        stream.Position = 0;
        return stream;
    }
}
