// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>Reads the asset feed through the rule of a property that references assets.</summary>
public static class AssetFeedExtensions
{
    /// <summary>Gets the feed's assets that a property accepts, keeping its current assignments.</summary>
    /// <param name="feed">The asset feed.</param>
    /// <param name="rule">What the property accepts.</param>
    /// <param name="keeping">The property's current assignments, listed even when the rule would not offer them.</param>
    /// <returns>The accepted and kept assets each time the feed publishes, ordered by name.</returns>
    public static IObservable<IReadOnlyList<AssetEntry>> Assets(this IAssetFeed feed, AssetRule rule, params IReadOnlyList<Uri?> keeping)
    {
        ArgumentNullException.ThrowIfNull(feed);
        ArgumentNullException.ThrowIfNull(rule);
        ArgumentNullException.ThrowIfNull(keeping);
        return feed.Items.Select(assets => rule.Apply(assets, keeping));
    }

    /// <summary>Finds the listed entry a reference denotes: by identity, then by cooked identity, then by logical path.</summary>
    /// <param name="entries">The entries the feed published.</param>
    /// <param name="uri">The asset reference.</param>
    /// <returns>The entry, or <see langword="null"/> when none is listed.</returns>
    public static AssetEntry? Find(this IReadOnlyList<AssetEntry> entries, Uri uri)
    {
        ArgumentNullException.ThrowIfNull(entries);
        ArgumentNullException.ThrowIfNull(uri);
        var logicalKey = GetLogicalKey(uri);
        return entries.FirstOrDefault(item => item.IdentityUri == uri)
            ?? entries.FirstOrDefault(item => item.CookedUri == uri)
            ?? entries.FirstOrDefault(item => string.Equals(GetLogicalKey(item.IdentityUri), logicalKey, StringComparison.OrdinalIgnoreCase));
    }

    private static string GetLogicalKey(Uri uri)
    {
        var path = AssetUriHelper.GetVirtualPath(uri);
        if (string.IsNullOrWhiteSpace(path))
        {
            path = uri.AbsolutePath;
        }

        return path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? path[..^".json".Length]
            : path;
    }
}
