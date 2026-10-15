// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;

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
}
