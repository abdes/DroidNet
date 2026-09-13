// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Groups proven engine contributions without merging independently authored identities or generator aliases.</summary>
internal static class AssetIdentityGrouping
{
    /// <summary>Returns one row per logical identity within the supplied browser scope.</summary>
    /// <param name="items">Rows already restricted to the relevant folder scope.</param>
    /// <returns>Canonical built-ins with their verified cooked companions, and all independent assets.</returns>
    internal static IReadOnlyList<ContentBrowserAssetItem> GroupBuiltins(IEnumerable<ContentBrowserAssetItem> items)
        => items.GroupBy(static item => (item.Kind, Identity: item.IsBuiltin ? item.BuiltinOriginUri ?? item.IdentityUri : item.IdentityUri))
            .Select(static group => Combine(group.Key.Identity, group.ToArray())).ToArray();

    /// <summary>Checks both the logical identity and its proven existing reference aliases.</summary>
    /// <param name="item">The projected logical asset.</param>
    /// <param name="identity">The reference being selected or invoked.</param>
    /// <returns>Whether this row represents that reference.</returns>
    internal static bool Represents(ContentBrowserAssetItem item, Uri identity)
        => item.IdentityUri == identity || item.CookedCompanions.Any(companion => companion.IdentityUri == identity);

    private static ContentBrowserAssetItem Combine(Uri identity, ContentBrowserAssetItem[] group)
    {
        var primary = group.FirstOrDefault(item => item.IdentityUri == identity) ?? group[0];
        if (group.Length == 1)
        {
            return primary;
        }

        var companions = group.Where(item => !ReferenceEquals(item, primary)).ToArray();
        var runtime = group.FirstOrDefault(static item => item.RuntimeAvailability == AssetRuntimeAvailability.Failed)
            ?? group.FirstOrDefault(static item => item.RuntimeAvailability == AssetRuntimeAvailability.Updating)
            ?? primary;
        return primary with
        {
            CookedCompanions = companions,
            RuntimeAvailability = runtime.RuntimeAvailability,
            RuntimeReason = runtime.RuntimeReason,
            DiagnosticCodes = group.SelectMany(static item => item.DiagnosticCodes).Distinct(StringComparer.Ordinal).ToArray(),
        };
    }
}
