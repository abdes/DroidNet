// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog.LooseCooked;

/// <summary>
/// A fixed catalog of one captured loose cooked index (<c>container.index.bin</c>); it never watches or reloads its
/// source, because cooked content changes only by publishing a new generation.
/// </summary>
/// <remarks>
/// This provider enumerates the keyed assets and the texture descriptors of the index, identified by the native virtual
/// paths it records (e.g. <c>/Content/Materials/Wood.omat</c>); a texture's record carries the facts its descriptor
/// file states.
/// </remarks>
public sealed class LooseCookedIndexAssetCatalog : IAssetCatalog
{
    private readonly Document document;
    private readonly FileSystemPath cookedRoot;
    private readonly Lock sync = new();
    private Task<IReadOnlyDictionary<AssetPath, AssetRecord>>? entries;

    /// <summary>Initializes a new instance of the <see cref="LooseCookedIndexAssetCatalog"/> class.</summary>
    /// <param name="document">The decoded index accepted by its content owner.</param>
    /// <param name="cookedRoot">The physical root named by that index.</param>
    public LooseCookedIndexAssetCatalog(Document document, FileSystemPath cookedRoot)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(cookedRoot);
        this.document = document;
        this.cookedRoot = cookedRoot;
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => Observable.Empty<AssetChange>();

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        Task<IReadOnlyDictionary<AssetPath, AssetRecord>> entries;
        lock (this.sync)
        {
            // The texture descriptors are read once, on first query, for every caller.
            entries = this.entries ??= CreateEntriesAsync(this.document, this.cookedRoot);
        }

        return AssetQueryMatcher.Apply(query, (await entries.WaitAsync(cancellationToken).ConfigureAwait(false)).Values);
    }

    private static async Task<byte[]?> ReadFileAsync(string path)
    {
        try
        {
            return await File.ReadAllBytesAsync(path).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static async Task<IReadOnlyDictionary<AssetPath, AssetRecord>> CreateEntriesAsync(Document document, FileSystemPath root)
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
            var texture = ReadTexture(await ReadFileAsync(Path.Combine(root.FullPath, file.RelativePath)).ConfigureAwait(false));
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
}
