// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Central policy for deciding when live-sync work can be replayed later.
/// </summary>
internal static class LiveSyncBufferingPolicy
{
    /// <summary>
    /// Returns <see langword="true"/> when a skipped property sync should be buffered.
    /// </summary>
    /// <param name="outcome">The classified live-sync outcome.</param>
    /// <returns><see langword="true"/> if the edit can be replayed after a full scene sync.</returns>
    public static bool ShouldBufferPropertySync(SyncOutcome outcome)
    {
        ArgumentNullException.ThrowIfNull(outcome);

        return outcome.Status == SyncStatus.SkippedNotRunning
               && string.Equals(outcome.Code, LiveSyncDiagnosticCodes.NotRunning, StringComparison.Ordinal);
    }
}
