// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Reactive.Linq;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>
/// Catalog provider for built-in generated assets.
/// </summary>
/// <remarks>
/// Generated assets are currently static for the lifetime of the process.
/// </remarks>
public sealed class GeneratedAssetCatalog : IAssetCatalog
{
    private readonly FrozenSet<AssetRecord> records;

    /// <summary>Initializes a new instance of the <see cref="GeneratedAssetCatalog"/> class from an engine-provided snapshot.</summary>
    /// <param name="records">The complete generated asset records for the current engine catalog.</param>
    public GeneratedAssetCatalog(IEnumerable<AssetRecord> records)
    {
        ArgumentNullException.ThrowIfNull(records);
        this.records = records.ToFrozenSet();
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => Observable.Empty<AssetChange>();

    /// <inheritdoc />
    public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        cancellationToken.ThrowIfCancellationRequested();

        return Task.FromResult(AssetQueryMatcher.Apply(query, this.records));
    }
}
