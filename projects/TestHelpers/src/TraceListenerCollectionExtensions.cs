// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.TestHelpers;

/// <summary>
/// Extension methods for the <see cref="TraceListenerCollection"/> class.
/// </summary>
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
