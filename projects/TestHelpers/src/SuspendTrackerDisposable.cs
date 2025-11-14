// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.TestHelpers;

/// <summary>
/// This is a helper class that allows us to suspend asserts / all trace listeners.
/// </summary>
public sealed class SuspendTrackerDisposable : IDisposable
{
    private readonly TraceListener[] suspendedListeners;
    private readonly TraceListenerCollection traceListenerCollection;

    /// <summary>
    /// Initializes a new instance of the <see cref="SuspendTrackerDisposable" />
    /// class.
    /// </summary>
    /// <param name="traceListenerCollection">The collection of trace listeners.</param>
    public SuspendTrackerDisposable(TraceListenerCollection traceListenerCollection)
    {
        this.traceListenerCollection = traceListenerCollection;

        var numListeners = traceListenerCollection.Count;
        this.suspendedListeners = new TraceListener[numListeners];
        for (var index = 0; index < numListeners; ++index)
        {
            this.suspendedListeners[index] = traceListenerCollection[index];
        }

        traceListenerCollection.Clear();
    }

    /// <inheritdoc/>
    public void Dispose() => this.traceListenerCollection.AddRange(this.suspendedListeners);
}
