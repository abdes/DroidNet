// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Terminal status for a live scene synchronization request.
/// </summary>
public enum SyncStatus
{
    Accepted = 0,
    SkippedNotRunning,
    Unsupported,
    Rejected,
    Failed,
    Cancelled,
}
