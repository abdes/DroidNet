// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TestHelpers;

using System.Diagnostics;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// A base class for test suites that have test cases checking for Debug
/// assertions in the code. Use the <see cref="TraceListener" /> to check if
/// assertions failed.
/// </summary>
public abstract class TestSuiteWithAssertions : IDisposable
{
    private readonly TraceListenerCollection? originalTraceListeners;

    /// <summary>
    /// Initializes a new instance of the <see cref="TestSuiteWithAssertions" /> class.
    /// </summary>
    /// <seealso href="https://learn.microsoft.com/en-us/visualstudio/test/using-microsoft-visualstudio-testtools-unittesting-members-in-unit-tests?view=vs-2022#test" />
    protected TestSuiteWithAssertions()
    {
        // Save and clear original trace listeners, add custom unit test trace listener.
        this.TraceListener = new DebugAssertUnitTestTraceListener();
        this.originalTraceListeners = Trace.Listeners;
        Trace.Listeners.Clear();
        _ = Trace.Listeners.Add(this.TraceListener);
    }

    protected DebugAssertUnitTestTraceListener TraceListener { get; }

    /// <inheritdoc />
    [TestCleanup]
    public void Dispose()
    {
        this.TraceListener.Clear();
        Trace.Listeners.Clear();
        if (this.originalTraceListeners != null)
        {
            Trace.Listeners.AddRange(this.originalTraceListeners);
        }

        GC.SuppressFinalize(this);
    }
}
