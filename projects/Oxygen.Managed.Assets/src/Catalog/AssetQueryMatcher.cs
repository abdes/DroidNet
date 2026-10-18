// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Applies a query to catalog records, the same way in every catalog.</summary>
internal static class AssetQueryMatcher
{
    /// <summary>Gets the records a query selects, in identity order.</summary>
    /// <param name="query">The query.</param>
    /// <param name="records">The catalog's records.</param>
    /// <returns>The records inside the query's scope that match its search text.</returns>
    public static IReadOnlyList<AssetRecord> Apply(AssetQuery query, IEnumerable<AssetRecord> records)
    {
        ArgumentNullException.ThrowIfNull(query);
        ArgumentNullException.ThrowIfNull(records);
        var term = query.SearchText?.Trim();
        return [.. records.Where(record => IsInScope(query.Scope, record) && (string.IsNullOrEmpty(term) || MatchesSearch(record, term)))
            .OrderBy(static record => record.Identity)];
    }

    private static bool IsInScope(AssetQueryScope scope, AssetRecord record)
        => scope.Traversal == AssetQueryTraversal.All || scope.Roots.Any(root => scope.Traversal switch
        {
            AssetQueryTraversal.Self => record.Identity == root,
            AssetQueryTraversal.Children => record.Identity.Parent == root,
            AssetQueryTraversal.Descendants => record.Identity != root && record.Identity.IsSameOrInside(root),
            _ => false,
        });

    // Search text is what a user typed into a search box, so it matches without regard to case.
    private static bool MatchesSearch(AssetRecord record, string term)
        => record.Identity.VirtualPath.Contains(term, StringComparison.OrdinalIgnoreCase)
            || record.Generated?.CanonicalName.Contains(term, StringComparison.OrdinalIgnoreCase) == true;
}
