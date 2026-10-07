// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Tests;

/// <summary>
/// Turns exceptions that escape into WinUI (event handlers, x:Bind updates, converters) into failures of the
/// running test instead of a fail-fast crash of the whole test host.
/// </summary>
/// <remarks>
/// WinUI reports such exceptions through <see cref="Microsoft.UI.Xaml.Application.UnhandledException"/> and
/// terminates the process (0xC000027B) unless the event is handled. The test host handles it, records the
/// exception here, faults the test currently awaited by <c>EnqueueAsync</c>, and fails the test in cleanup.
/// All members are used on the UI thread.
/// </remarks>
internal static class UnhandledUiExceptions
{
    private static readonly List<Exception> Recorded = [];
    private static TaskCompletionSource current = NewSignal();

    /// <summary>Gets a task that faults when the current test raises an unhandled UI exception.</summary>
    public static Task Current => current.Task;

    /// <summary>Starts a new test with no recorded exceptions.</summary>
    public static void Reset()
    {
        Recorded.Clear();
        current = NewSignal();
    }

    /// <summary>Records an exception WinUI could not deliver to any caller.</summary>
    /// <param name="message">The message WinUI reports; it survives even when the exception is a bare COM error.</param>
    /// <param name="exception">The escaped exception.</param>
    public static void Record(string message, Exception exception)
    {
        var failure = new InvalidOperationException($"Unhandled UI exception: {message}", exception);
        Recorded.Add(failure);
        _ = current.TrySetException(failure);
        Console.Error.WriteLine(failure);
    }

    /// <summary>Throws when the finishing test raised any unhandled UI exception.</summary>
    /// <exception cref="AggregateException">One or more unhandled UI exceptions were recorded.</exception>
    public static void ThrowIfAny()
    {
        if (Recorded.Count == 0)
        {
            return;
        }

        var failures = Recorded.ToArray();
        Reset();
        throw new AggregateException("The test raised unhandled UI exceptions.", failures);
    }

    private static TaskCompletionSource NewSignal() => new(TaskCreationOptions.RunContinuationsAsynchronously);
}
