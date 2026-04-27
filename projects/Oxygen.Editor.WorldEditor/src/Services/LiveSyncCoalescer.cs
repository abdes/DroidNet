// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

internal readonly record struct SyncCoalescingKey(Guid SceneId, Guid NodeId);

/// <summary>
/// Deterministic preview-sync throttle used by edit sessions.
/// </summary>
internal sealed class LiveSyncCoalescer
{
    public static readonly TimeSpan DefaultPreviewInterval = TimeSpan.FromMilliseconds(16);

    private readonly TimeSpan previewInterval;
    private readonly Dictionary<SyncCoalescingKey, DateTimeOffset> lastPreviewByKey = [];

    public LiveSyncCoalescer(TimeSpan? previewInterval = null)
    {
        var interval = previewInterval ?? DefaultPreviewInterval;
        if (interval <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(previewInterval), "Preview interval must be positive.");
        }

        this.previewInterval = interval;
    }

    public bool ShouldIssuePreview(SyncCoalescingKey key, DateTimeOffset observedAt)
    {
        if (!this.lastPreviewByKey.TryGetValue(key, out var lastPreview) ||
            observedAt - lastPreview >= this.previewInterval)
        {
            this.lastPreviewByKey[key] = observedAt;
            return true;
        }

        return false;
    }

    public bool CompleteTerminalSync(SyncCoalescingKey key)
    {
        _ = this.lastPreviewByKey.Remove(key);
        return true;
    }

    public void Cancel(SyncCoalescingKey key)
        => _ = this.lastPreviewByKey.Remove(key);
}
