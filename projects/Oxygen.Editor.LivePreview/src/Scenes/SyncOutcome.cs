// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>How the engine took one projected edit, and what to report when it did not take it.</summary>
/// <param name="Status">How the engine took the edit.</param>
/// <param name="OperationKind">The kind of edit, which names the operation result that reports a failure.</param>
/// <param name="Scope">The scene, node and component the edit targeted.</param>
/// <param name="Code">The diagnostic code of any status but <see cref="SyncStatus.Accepted"/>.</param>
/// <param name="Message">What happened, for the user, with the code.</param>
/// <param name="Exception">The exception behind the status, when one was thrown.</param>
internal sealed record SyncOutcome(
    SyncStatus Status,
    string OperationKind,
    AffectedScope Scope,
    string? Code = null,
    string? Message = null,
    Exception? Exception = null);
