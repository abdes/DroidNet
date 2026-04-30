// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Result returned by scene document commands.
/// </summary>
public sealed record SceneCommandResult(bool Succeeded, Guid? OperationResultId = null)
{
    /// <summary>
    /// Gets a successful command result.
    /// </summary>
    public static SceneCommandResult Success { get; } = new(true);
}
