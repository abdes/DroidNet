// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
///     Provides the pending property-sync count for a scene after the buffered live-sync queue changes.
/// </summary>
/// <param name="sceneId">The scene whose pending sync count changed.</param>
/// <param name="pendingCount">The number of pending property sync requests for the scene.</param>
public sealed class PendingPropertySyncCountChangedEventArgs(Guid sceneId, int pendingCount) : EventArgs
{
    /// <summary>
    ///     Gets the scene whose pending sync count changed.
    /// </summary>
    public Guid SceneId { get; } = sceneId;

    /// <summary>
    ///     Gets the number of pending property sync requests for the scene.
    /// </summary>
    public int PendingCount { get; } = pendingCount;
}
