// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.CSharp.RuntimeBinder;

namespace DroidNet.TimeMachine;

/// <summary>
/// Manages the transition of a state for a target object, ensuring the previous state is restored when disposed.
/// </summary>
/// <typeparam name="T">The type of the state.</typeparam>
/// <remarks>
/// The <see cref="StateTransition{T}"/> class is used to manage the transition of a state for a target object.
/// It ensures that the previous state is restored when the transition is disposed, making it useful for scenarios
/// where temporary state changes are needed.
/// </remarks>
internal sealed class StateTransition<T> : IDisposable
{
    private readonly T previousState;
    private readonly dynamic target;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="StateTransition{T}"/> class with the specified target and new state.
    /// </summary>
    /// <param name="target">The target object whose state will be managed.</param>
    /// <param name="newState">The new state to set for the target object.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown if the target is null, the target does not have a State property, or the State property is null.
    /// </exception>
    public StateTransition(dynamic target, T newState)
    {
        this.target = target ?? throw new InvalidOperationException($"{nameof(target)} cannot be null");
        try
        {
            this.previousState = target.State ?? throw new InvalidOperationException("State property cannot be null");
            target.State = newState ?? throw new InvalidOperationException($"{nameof(newState)} cannot be null");
        }
        catch (RuntimeBinderException)
        {
            throw new InvalidOperationException("target must have a State property");
        }
    }

    /// <summary>
    /// Disposes the state transition, restoring the previous state.
    /// </summary>
    public void Dispose() => this.Dispose(disposing: true);

    private void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // Restore the previous state
            Debug.Assert(
                this.previousState is not null,
                "previous state should not be null, throw in the constructor if it is");
            this.target.State = this.previousState;
        }

        this.disposed = true;
    }
}
