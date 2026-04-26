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
    /// Successful command result.
    /// </summary>
    public static SceneCommandResult Success { get; } = new(true);
}

/// <summary>
/// Result returned by scene document commands that produce a value.
/// </summary>
/// <typeparam name="T">The produced value type.</typeparam>
public sealed record SceneCommandResult<T>(bool Succeeded, T? Value, Guid? OperationResultId = null)
{
    /// <summary>
    /// Creates a successful result.
    /// </summary>
    /// <param name="value">The produced value.</param>
    /// <returns>The command result.</returns>
    public static SceneCommandResult<T> Success(T value) => new(true, value);

    /// <summary>
    /// Creates a failed result.
    /// </summary>
    /// <param name="operationResultId">The visible operation-result identity, when one was published.</param>
    /// <returns>The command result.</returns>
    public static SceneCommandResult<T> Failure(Guid? operationResultId = null) => new(false, default, operationResultId);
}
