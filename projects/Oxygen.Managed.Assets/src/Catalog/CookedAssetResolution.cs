// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Projects unqualified native lookup using last-mounted path resolution followed by asset-key resolution.</summary>
public static class CookedAssetResolution
{
    /// <summary>Resolves indexed sources before applying a browser query.</summary>
    /// <param name="records">Complete source snapshots in mount order, lowest priority first.</param>
    /// <param name="query">The query applied to resolved logical identities.</param>
    /// <returns>One row per logical URI with the effective native source and masked representations.</returns>
    public static IReadOnlyList<AssetRecord> Resolve(IEnumerable<AssetRecord> records, AssetQuery query)
    {
        ArgumentNullException.ThrowIfNull(records);
        ArgumentNullException.ThrowIfNull(query);
        var snapshot = records.ToArray();
        var native = snapshot.Where(static record => record.Cooked is not null).ToArray();
        var byKey = native.GroupBy(static record => record.Cooked!.AssetKey).ToDictionary(static group => group.Key, static group => group.ToArray());
        return AssetQueryMatcher.Apply(query, snapshot.GroupBy(static record => record.Identity).Select(group =>
        {
            var pathWinner = group.LastOrDefault(static record => record.Cooked is not null);
            if (pathWinner?.Cooked is not { } indexed)
            {
                return group.First();
            }

            var keyVersions = byKey[indexed.AssetKey];
            var winner = keyVersions[^1].Cooked!;
            var overridden = group.Concat(keyVersions).Select(static record => record.Cooked).OfType<CookedAssetMetadata>()
                .Where(candidate => candidate != winner).Distinct().ToArray();
            return pathWinner with { Cooked = winner, OverriddenCookedSources = overridden };
        }));
    }
}
