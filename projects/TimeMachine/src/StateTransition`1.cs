// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TimeMachine;

using System.Diagnostics;
using Microsoft.CSharp.RuntimeBinder;

internal sealed class StateTransition<T> : IDisposable
{
    private readonly T previousState;
    private readonly dynamic target;
    private bool disposed;

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
