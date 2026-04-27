// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Aggregate result for environment live-sync, where unsupported engine APIs are reported per field.
/// </summary>
public sealed record EnvironmentSyncResult(
    SyncStatus Overall,
    IReadOnlyDictionary<string, SyncStatus> PerField);
