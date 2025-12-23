// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Reactive.Linq;

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Catalog provider for built-in generated assets.
/// </summary>
/// <remarks>
/// Generated assets are currently static for the lifetime of the process.
/// </remarks>
public sealed class GeneratedAssetCatalog : IAssetCatalog
{
    private readonly FrozenSet<AssetRecord> records;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeneratedAssetCatalog"/> class.
    /// </summary>
    public GeneratedAssetCatalog()
    {
        this.records = BuiltInAssets.Create()
            .Select(a => new AssetRecord(a.Uri))
            .ToFrozenSet();
    }

    /// <inheritdoc />
    public IObservable<AssetChange> Changes => Observable.Empty<AssetChange>();

    /// <inheritdoc />
    public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(query);
        cancellationToken.ThrowIfCancellationRequested();

        IEnumerable<AssetRecord> results = this.records;

        results = results.Where(r => AssetQueryScopeMatcher.IsMatch(query.Scope, r.Uri));

        if (!string.IsNullOrWhiteSpace(query.SearchText))
        {
            var term = query.SearchText.Trim();
            results = results.Where(r => MatchesSearch(r, term));
        }

        var list = results
            .OrderBy(r => r.Uri.ToString(), StringComparer.Ordinal)
            .ToArray();

        return Task.FromResult<IReadOnlyList<AssetRecord>>(list);
    }

    // Provider-defined semantics (intentionally small and stable).
    private static bool MatchesSearch(AssetRecord record, string term)
        => record.Name.Contains(term, StringComparison.OrdinalIgnoreCase)
            || record.Uri.ToString().Contains(term, StringComparison.OrdinalIgnoreCase)
            || record.Uri.Authority.Contains(term, StringComparison.OrdinalIgnoreCase);
}
