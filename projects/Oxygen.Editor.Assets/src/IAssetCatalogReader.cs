// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>What the asset feed reads from the asset catalog.</summary>
internal interface IAssetCatalogReader
{
    /// <summary>Gets the catalog's record changes.</summary>
    public IObservable<AssetChange> Changes { get; }

    /// <summary>Brings the catalog up to date with the project's current publication and files.</summary>
    /// <param name="cancellationToken">Cancels the refresh.</param>
    /// <returns>Completion after the catalog is current.</returns>
    public Task RefreshAsync(CancellationToken cancellationToken = default);

    /// <summary>Reads the catalog's records together with the publication they were read from.</summary>
    /// <param name="query">The records to read.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>A snapshot, disposed after the reads that depend on its publication finish.</returns>
    public Task<AssetCatalogSnapshot> ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken = default);
}
