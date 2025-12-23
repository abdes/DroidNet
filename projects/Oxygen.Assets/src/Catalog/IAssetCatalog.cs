// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Provides asset enumeration/search (catalog) services.
/// </summary>
/// <remarks>
/// <para>
/// This interface is intentionally separate from <see cref="IAssetService"/>, which loads a specific
/// asset by URI. A catalog answers "given a scope and criteria, enumerate assets".
/// </para>
/// <para>
/// The scope is always client-controlled via <see cref="AssetQuery"/>. This allows different
/// consumers (Content Browser, property editors, etc.) to query within a selected folder, a mount
/// point, or the entire registry.
/// </para>
/// </remarks>
public interface IAssetCatalog
{
    /// <summary>
    /// Gets an observable stream of catalog changes.
    /// </summary>
    /// <remarks>
    /// Providers may merge multiple change sources (filesystem watchers, container index updates,
    /// generated assets). Consumers are expected to apply changes incrementally to cached results.
    /// </remarks>
    public IObservable<AssetChange> Changes { get; }

    /// <summary>
    /// Asynchronously queries the catalog.
    /// </summary>
    /// <param name="query">The query, including a client-specified scope.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>A list of matching assets.</returns>
    public Task<IReadOnlyList<AssetRecord>> QueryAsync(
        AssetQuery query,
        CancellationToken cancellationToken = default);
}
