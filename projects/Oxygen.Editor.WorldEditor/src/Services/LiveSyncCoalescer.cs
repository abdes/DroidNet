// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Deterministic preview-sync throttle used by edit sessions.
/// </summary>
internal sealed class LiveSyncCoalescer
{
    /// <summary>The minimum interval between previews for the same node.</summary>
    public static readonly TimeSpan DefaultPreviewInterval = TimeSpan.FromMilliseconds(16);

    private readonly Lock gate = new();
    private readonly TimeSpan previewInterval;
    private readonly Dictionary<SyncCoalescingKey, DateTimeOffset> lastPreviewByKey = [];

    /// <summary>Initializes a new instance of the <see cref="LiveSyncCoalescer"/> class.</summary>
    /// <param name="previewInterval">An optional interval used by deterministic validation.</param>
    public LiveSyncCoalescer(TimeSpan? previewInterval = null)
    {
        var interval = previewInterval ?? DefaultPreviewInterval;
        if (interval <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(previewInterval), "Preview interval must be positive.");
        }

        this.previewInterval = interval;
    }

    /// <summary>Admits a preview only when its node's interval has elapsed.</summary>
    /// <param name="key">The scene and node.</param>
    /// <param name="observedAt">The sample time.</param>
    /// <returns>Whether this sample should be delivered.</returns>
    public bool ShouldIssuePreview(SyncCoalescingKey key, DateTimeOffset observedAt)
    {
        lock (this.gate)
        {
            if (!this.lastPreviewByKey.TryGetValue(key, out var lastPreview) ||
                observedAt - lastPreview >= this.previewInterval)
            {
                this.lastPreviewByKey[key] = observedAt;
                return true;
            }

            return false;
        }
    }

    /// <summary>Always admits a terminal update and resets its preview interval.</summary>
    /// <param name="key">The completed node gesture.</param>
    /// <returns>True for the mandatory terminal delivery.</returns>
    public bool CompleteTerminalSync(SyncCoalescingKey key)
    {
        this.Cancel(key);
        return true;
    }

    /// <summary>Clears preview timing for a cancelled gesture.</summary>
    /// <param name="key">The cancelled node gesture.</param>
    public void Cancel(SyncCoalescingKey key)
    {
        lock (this.gate)
        {
            _ = this.lastPreviewByKey.Remove(key);
        }
    }

    /// <summary>Discards preview timing inherited from a retired scene activation.</summary>
    /// <param name="sceneId">The retired scene.</param>
    public void ResetScene(Guid sceneId)
    {
        lock (this.gate)
        {
            foreach (var key in this.lastPreviewByKey.Keys.Where(key => key.SceneId == sceneId).ToArray())
            {
                _ = this.lastPreviewByKey.Remove(key);
            }
        }
    }
}
