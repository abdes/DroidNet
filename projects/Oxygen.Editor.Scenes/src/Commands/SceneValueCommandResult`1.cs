// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Result returned by scene document commands that produce a value.
/// </summary>
/// <typeparam name="T">The produced value type.</typeparam>
public sealed record SceneValueCommandResult<T>(bool Succeeded, T? Value, Guid? OperationResultId = null)
{
    /// <summary>Gets the failure message for the initiating authoring control.</summary>
    public string? FailureMessage { get; init; }
}
