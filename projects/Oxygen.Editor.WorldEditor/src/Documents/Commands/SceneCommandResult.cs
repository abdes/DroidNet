// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Result returned by scene document commands.
/// </summary>
public sealed record SceneCommandResult(bool Succeeded, Guid? OperationResultId = null)
{
    /// <summary>Gets a value indicating whether a successful save left newer authoring changes unsaved.</summary>
    public bool HasUnsavedChanges { get; init; }

    /// <summary>
    /// Gets a successful command result.
    /// </summary>
    public static SceneCommandResult Success { get; } = new(Succeeded: true);
}
