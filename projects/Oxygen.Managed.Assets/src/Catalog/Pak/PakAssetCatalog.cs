// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog.Pak;

/// <summary>
/// A fixed catalog of the assets and textures a pak's browse index lists; a pak is read-only, so the catalog never
/// changes.
/// </summary>
public sealed class PakAssetCatalog : IAssetCatalog
{
    private readonly IReadOnlyDictionary<AssetPath, AssetRecord> records;

    /// <summary>Initializes a new instance of the <see cref="PakAssetCatalog"/> class.</summary>
    /// <param name="document">The pak metadata read by <see cref="PakIndex.Read"/>.</param>
    /// <param name="pakPath">The pak file the metadata was read from.</param>
    /// <exception cref="InvalidDataException">The pak lists a path that is not the virtual path of an asset.</exception>
    public PakAssetCatalog(PakDocument document, FileSystemPath pakPath)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(pakPath);
        var records = new CookedAssetRecords();
        foreach (var asset in document.Assets)
        {
            records.Add(asset.VirtualPath, identity => new(pakPath, null, document.SourceIdentity, asset.AssetKey, asset.AssetType, asset.DescriptorSize, null)
            {
                VirtualPath = identity,
                References = asset.References,
            });
        }

        foreach (var texture in document.Textures)
        {
            records.Add(texture.VirtualPath, identity => new(pakPath, null, document.SourceIdentity, AssetKey.FromVirtualPath(texture.VirtualPath), (byte)AssetType.Unknown, TextureResourceRecord.Size, null)
            {
                VirtualPath = identity,
                Texture = texture.Texture,
            });
        }

        this.records = records.Records;
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => Observable.Empty<AssetChange>();

    /// <inheritdoc />
    public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(AssetQueryMatcher.Apply(query, this.records.Values));
    }
}
