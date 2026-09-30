// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <summary>
/// A project catalog bound to its accepted content publication.
/// </summary>
public interface IProjectAssetCatalog : IAssetCatalog
{
    /// <summary>
    /// Initializes the catalog by indexing the project root and mount points.
    /// </summary>
    /// <returns>A task that completes when initialization is done.</returns>
    Task InitializeAsync();

    /// <summary>Captures records and their shared publication for status and logical-folder projection.</summary>
    /// <param name="query">The requested catalog scope.</param>
    /// <param name="cancellationToken">Cancels discovery.</param>
    /// <returns>An owned snapshot disposed after dependent reads finish.</returns>
    Task<ProjectAssetSnapshot> ReadSnapshotAsync(AssetQuery query, CancellationToken cancellationToken = default);

    /// <summary>
    /// Refreshes any underlying catalogs that expose an explicit refresh capability.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when refreshable catalog snapshots are current.</returns>
    Task RefreshAsync(CancellationToken cancellationToken = default);

    /// <summary>Refreshes from the exact publication accepted by the runtime.</summary>
    /// <param name="publication">A borrowed, coherent selected publication.</param>
    /// <param name="cancellationToken">Cancels catalog preparation.</param>
    /// <returns>Completion after the accepted snapshot is visible.</returns>
    Task RefreshAsync(Oxygen.Editor.ContentPipeline.Publication.CookPublicationReadLease publication, CancellationToken cancellationToken = default);

}
