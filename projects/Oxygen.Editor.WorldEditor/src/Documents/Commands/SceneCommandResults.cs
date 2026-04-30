// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Factory helpers for scene command results that carry values.
/// </summary>
public static class SceneCommandResults
{
    /// <summary>
    /// Creates a successful result.
    /// </summary>
    /// <typeparam name="T">The produced value type.</typeparam>
    /// <param name="value">The produced value.</param>
    /// <returns>The command result.</returns>
    public static SceneValueCommandResult<T> Success<T>(T value) => new(true, value);

    /// <summary>
    /// Creates a failed result.
    /// </summary>
    /// <typeparam name="T">The produced value type.</typeparam>
    /// <param name="operationResultId">The visible operation-result identity, when one was published.</param>
    /// <returns>The command result.</returns>
    public static SceneValueCommandResult<T> Failure<T>(Guid? operationResultId = null) => new(false, default, operationResultId);
}
