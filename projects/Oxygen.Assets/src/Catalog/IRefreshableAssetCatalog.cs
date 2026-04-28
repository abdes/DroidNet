// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Optional catalog capability for explicitly refreshing an already-initialized snapshot.
/// </summary>
/// <remarks>
/// File-system watchers are best-effort and can lag behind editor-authored writes. Consumers that
/// are handling an explicit user refresh or an editor-authored asset change can use this capability
/// to force the catalog snapshot to catch up before querying it.
/// </remarks>
public interface IRefreshableAssetCatalog
{
    /// <summary>
    /// Refreshes the catalog's internal snapshot.
    /// </summary>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A task that completes when the refresh has finished.</returns>
    public Task RefreshAsync(CancellationToken cancellationToken = default);
}
