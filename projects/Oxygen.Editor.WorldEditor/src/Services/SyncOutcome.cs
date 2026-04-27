// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Result of projecting one committed authoring edit into the live engine.
/// </summary>
public sealed record SyncOutcome(
    SyncStatus Status,
    string OperationKind,
    AffectedScope Scope,
    string? Code = null,
    string? Message = null,
    Exception? Exception = null);
