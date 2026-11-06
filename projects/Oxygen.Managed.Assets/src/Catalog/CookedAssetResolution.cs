// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Projects native lookup: an identity is an asset of one mount, so the last mounted record of it is the asset.</summary>
public static class CookedAssetResolution
{
    /// <summary>Resolves indexed sources before applying a browser query.</summary>
    /// <param name="records">Complete source snapshots in mount order, lowest priority first.</param>
    /// <param name="query">The query applied to resolved logical identities.</param>
    /// <returns>One row per logical URI, with its native source when it has one.</returns>
    public static IReadOnlyList<AssetRecord> Resolve(IEnumerable<AssetRecord> records, AssetQuery query)
    {
        ArgumentNullException.ThrowIfNull(records);
        ArgumentNullException.ThrowIfNull(query);
        return AssetQueryMatcher.Apply(query, records.GroupBy(static record => record.Identity).Select(static group =>
            group.LastOrDefault(static record => record.Cooked is not null) ?? group.First()));
    }
}
