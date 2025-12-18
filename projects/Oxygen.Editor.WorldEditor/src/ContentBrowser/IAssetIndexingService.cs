// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Service for indexing and monitoring project assets.
/// </summary>
public interface IAssetIndexingService : IDisposable
{
    /// <summary>
    /// Gets the current indexing status.
    /// </summary>
    public IndexingStatus Status { get; }

    /// <summary>
    /// Gets observable stream of asset changes (Added/Removed/Modified).
    /// Uses ReplaySubject(500) to buffer recent changes for late subscribers.
    /// </summary>
    /// <remarks>
    /// <para>
    /// This observable uses a ReplaySubject with a buffer of 500 notifications to ensure
    /// subscribers don't miss recent changes that occurred before subscription.
    /// </para>
    /// <para>
    /// <strong>Important for snapshot + stream pattern:</strong> If you first call
    /// <see cref="QueryAssetsAsync"/> to get a snapshot, then subscribe to this observable,
    /// you may receive duplicate notifications for assets already in your snapshot due to
    /// the replay buffer. To handle this:
    /// </para>
    /// <code>
    /// // Get initial snapshot
    /// var snapshot = await indexer.QueryAssetsAsync(a => a.AssetType == AssetType.Mesh);
    /// var seenAssets = new HashSet&lt;string&gt;(snapshot.Select(a => a.Location));
    ///
    /// // Subscribe to changes, deduplicating replayed items
    /// subscription = indexer.AssetChanges
    ///     .Where(n => n.ChangeType == AssetChangeType.Added)
    ///     .Where(n => !seenAssets.Contains(n.Asset.Location))
    ///     .Subscribe(n => ProcessNewAsset(n.Asset));
    /// </code>
    /// <para>
    /// Alternatively, skip deduplication if you can tolerate duplicate processing or
    /// use an idempotent update mechanism (e.g., dictionary-based collections).
    /// </para>
    /// </remarks>
    public IObservable<AssetChangeNotification> AssetChanges { get; }

    /// <summary>
    /// Starts background indexing. Returns when initial scan completes.
    /// File watching continues after completion for real-time updates.
    /// </summary>
    /// <param name="progress">Optional progress reporter for UI.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>Task that completes when initial indexing finishes.</returns>
    public Task StartIndexingAsync(
        IProgress<IndexingProgress>? progress = null,
        CancellationToken ct = default);

    /// <summary>
    /// Stops background indexing and file watching.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task StopIndexingAsync();

    /// <summary>
    /// Query assets with optional filtering. Blocks until indexing completes if still in progress.
    /// Returns deterministic snapshot at query time.
    /// </summary>
    /// <remarks>
    /// This method provides a thread-safe snapshot of assets matching the predicate.
    /// If you combine this with subscribing to <see cref="AssetChanges"/>, note that
    /// the ReplaySubject may contain some assets that are already in your snapshot.
    /// Use a HashSet with asset identity equality or deduplicate based on Location to avoid duplicates.
    /// </remarks>
    /// <param name="predicate">Optional filter predicate. If null, returns all assets.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>Read-only list of matching assets at query time.</returns>
    public Task<IReadOnlyList<GameAsset>> QueryAssetsAsync(
        Func<GameAsset, bool>? predicate = null,
        CancellationToken ct = default);
}
