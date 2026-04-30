// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Stores property sync requests that should replay after the next full scene sync.
/// </summary>
internal sealed class PendingPropertySyncQueue
{
    private readonly Lock gate = new();
    private readonly Dictionary<Guid, List<PendingPropertySyncEntry>> entriesByScene = [];

    /// <summary>
    /// Adds one pending property sync request.
    /// </summary>
    /// <param name="sceneId">The scene that owns the pending request.</param>
    /// <param name="nodeId">The target node id.</param>
    /// <param name="entries">The property values to snapshot and replay later.</param>
    /// <returns>The number of pending requests for the scene.</returns>
    public int Enqueue(Guid sceneId, Guid nodeId, IReadOnlyList<EnginePropertyValueEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);

        if (entries.Count == 0)
        {
            return this.Count(sceneId);
        }

        var entrySnapshot = new EnginePropertyValueEntry[entries.Count];
        for (var i = 0; i < entries.Count; i++)
        {
            entrySnapshot[i] = entries[i];
        }

        lock (this.gate)
        {
            if (!this.entriesByScene.TryGetValue(sceneId, out var pendingEntries))
            {
                pendingEntries = [];
                this.entriesByScene.Add(sceneId, pendingEntries);
            }

            pendingEntries.Add(new PendingPropertySyncEntry(nodeId, entrySnapshot));
            return pendingEntries.Count;
        }
    }

    /// <summary>
    /// Removes and returns every pending request for a scene.
    /// </summary>
    /// <param name="sceneId">The scene whose pending requests should be drained.</param>
    /// <returns>The pending requests in enqueue order.</returns>
    public IReadOnlyList<PendingPropertySyncEntry> Drain(Guid sceneId)
    {
        lock (this.gate)
        {
            if (!this.entriesByScene.Remove(sceneId, out var pendingEntries))
            {
                return [];
            }

            return pendingEntries.ToArray();
        }
    }

    /// <summary>
    /// Gets the number of pending requests for a scene.
    /// </summary>
    /// <param name="sceneId">The scene id.</param>
    /// <returns>The pending request count.</returns>
    public int Count(Guid sceneId)
    {
        lock (this.gate)
        {
            return this.entriesByScene.TryGetValue(sceneId, out var pendingEntries)
                ? pendingEntries.Count
                : 0;
        }
    }
}
