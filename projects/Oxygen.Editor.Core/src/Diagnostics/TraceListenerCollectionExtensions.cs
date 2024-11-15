// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Core.Diagnostics;

using System.Diagnostics;

public static class TraceListenerCollectionExtensions
{
    /// <summary>
    /// Create a <see cref="IDisposable" /> helper that can be used within  code scope
    /// to temporarily suspend all trace listeners and avoid <c>Debug.Assert()</c> to
    /// fail the tests when they are expected.
    /// </summary>
    /// Use it like this to suspend assertions for the code within the scope:
    /// <code>
    /// ProjectBrowserSettings settings;
    /// using (Trace.Listeners.AssertSuspend())
    /// {
    ///     // Assertions are disable until we leave the scope
    ///     settings = new ProjectBrowserSettings() { BuiltinTemplates = templates };
    /// }
    /// // Now assertions are enabled again.
    /// </code>
    /// <param name="traceListenerCollection">
    /// The collection of trace listeners to
    /// suspend.
    /// </param>
    /// <returns>
    /// A <see cref="IDisposable" /> to help restore the listeners after we leave the
    /// <c>using...</c> scope.
    /// </returns>
    public static SuspendTrackerDisposable AssertSuspend(this TraceListenerCollection traceListenerCollection)
        => new(traceListenerCollection);
}

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

    public void Dispose()
    {
        GC.SuppressFinalize(this);
        this.traceListenerCollection.AddRange(this.suspendedListeners);
    }
}
