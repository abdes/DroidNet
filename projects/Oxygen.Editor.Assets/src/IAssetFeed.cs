// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets;

/// <summary>
/// The editor's live view of the active project's assets: project assets, mounted libraries and engine built-ins,
/// one entry per logical asset.
/// </summary>
public interface IAssetFeed
{
    /// <summary>Gets the current entries; a new list is published whenever an asset or its state changes.</summary>
    public IObservable<IReadOnlyList<AssetEntry>> Items { get; }

    /// <summary>Refreshes the entries from the catalogs and cook status.</summary>
    /// <param name="cancellationToken">Cancels the refresh.</param>
    /// <returns>Completion once the refreshed entries are published.</returns>
    public Task RefreshAsync(CancellationToken cancellationToken = default);

    /// <summary>Resolves the entry a reference denotes, including references to missing assets.</summary>
    /// <param name="uri">The asset reference.</param>
    /// <param name="cancellationToken">Cancels the resolution.</param>
    /// <returns>The entry, or <see langword="null"/> when the active project changed during resolution.</returns>
    public Task<AssetEntry?> ResolveAsync(Uri uri, CancellationToken cancellationToken = default);
}
