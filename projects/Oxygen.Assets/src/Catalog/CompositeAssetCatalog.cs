// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;

namespace Oxygen.Assets.Catalog;

/// <summary>
/// A catalog that composes multiple underlying catalogs.
/// </summary>
/// <remarks>
/// <para>
/// This type is the durable building block for a multi-provider world: generated assets, filesystem
/// assets, PAK/container assets, etc.
/// </para>
/// <para>
/// Query results are merged and de-duplicated by URI identity.
/// Change streams are merged into a single observable.
/// </para>
/// </remarks>
public sealed class CompositeAssetCatalog : IAssetCatalog
{
    private readonly IReadOnlyList<IAssetCatalog> catalogs;
    private readonly Lazy<IObservable<AssetChange>> changes;

    /// <summary>
    /// Initializes a new instance of the <see cref="CompositeAssetCatalog"/> class.
    /// </summary>
    /// <param name="catalogs">The catalogs to compose.</param>
    public CompositeAssetCatalog(IEnumerable<IAssetCatalog> catalogs)
    {
        ArgumentNullException.ThrowIfNull(catalogs);
        this.catalogs = [.. catalogs];

        this.changes = new Lazy<IObservable<AssetChange>>(
            () => BuildChanges(this.catalogs),
            isThreadSafe: true);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="CompositeAssetCatalog"/> class.
    /// </summary>
    /// <param name="catalogs">The catalogs to compose.</param>
    public CompositeAssetCatalog(params IAssetCatalog[] catalogs)
        : this((IEnumerable<IAssetCatalog>)catalogs)
    {
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => this.changes.Value;

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);

        if (this.catalogs.Count == 0)
        {
            return [];
        }

        var results = await Task.WhenAll(
                this.catalogs.Select(c => c.QueryAsync(query, cancellationToken)))
            .ConfigureAwait(false);

        // Merge and de-duplicate by URI identity.
        // Prefer a stable, deterministic ordering (useful for tests and UI).
        return results
            .SelectMany(r => r)
            .DistinctBy(r => r.Uri, AssetUriComparer.Instance)
            .OrderBy(r => r.Uri.ToString(), StringComparer.Ordinal)
            .ToArray();
    }

    private static IObservable<AssetChange> BuildChanges(IReadOnlyList<IAssetCatalog> catalogs)
    {
        if (catalogs.Count == 0)
        {
            return Observable.Empty<AssetChange>();
        }

        // Merge provider streams and share a single subscription among observers.
        // A provider stream can be hot/cold; this avoids repeated subscriptions per consumer.
        return catalogs.Select(c => c.Changes)
            .Merge()
            .Publish()
            .RefCount();
    }

    private sealed class AssetUriComparer : IEqualityComparer<Uri>
    {
        public static AssetUriComparer Instance { get; } = new();

        public bool Equals(Uri? x, Uri? y)
        {
            if (ReferenceEquals(x, y))
            {
                return true;
            }

            if (x is null || y is null)
            {
                return false;
            }

            return string.Equals(x.Scheme, y.Scheme, StringComparison.OrdinalIgnoreCase)
                && string.Equals(x.Authority, y.Authority, StringComparison.OrdinalIgnoreCase)
                && string.Equals(x.AbsolutePath, y.AbsolutePath, StringComparison.Ordinal);
        }

        public int GetHashCode(Uri obj)
        {
            ArgumentNullException.ThrowIfNull(obj);

            var hash = default(HashCode);
            hash.Add(obj.Scheme, StringComparer.OrdinalIgnoreCase);
            hash.Add(obj.Authority, StringComparer.OrdinalIgnoreCase);
            hash.Add(obj.AbsolutePath, StringComparer.Ordinal);
            return hash.ToHashCode();
        }
    }
}
