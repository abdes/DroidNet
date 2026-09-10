// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>An immutable lifecycle notification correlated with its originating runtime run.</summary>
/// <param name="runId">The run identity, or empty before the first loop starts.</param>
/// <param name="previousState">The state before the transition.</param>
/// <param name="state">The state after the transition.</param>
/// <param name="operationResult">The structured result for unexpected loop termination.</param>
/// <param name="exception">The original loop exception, when one was thrown.</param>
public sealed class EngineStateChangedEventArgs(
    Guid runId,
    EngineServiceState previousState,
    EngineServiceState state,
    OperationResult? operationResult = null,
    Exception? exception = null) : EventArgs
{
    /// <summary>Gets the originating runtime run.</summary>
    public Guid RunId { get; } = runId;

    /// <summary>Gets the state before this transition.</summary>
    public EngineServiceState PreviousState { get; } = previousState;

    /// <summary>Gets the state after this transition.</summary>
    public EngineServiceState State { get; } = state;

    /// <summary>Gets the structured unexpected-termination result, if any.</summary>
    public OperationResult? OperationResult { get; } = operationResult;

    /// <summary>Gets the original exception without wrapping or replacing it.</summary>
    public Exception? Exception { get; } = exception;
}
