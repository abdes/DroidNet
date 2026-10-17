// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.LivePreview;

/// <summary>The thread that owns a document or a session, through the synchronization context it captured.</summary>
/// <remarks>
/// LivePreview raises its events and reads authoring state on the owner thread. A capture without a synchronization
/// context, as in a test that installs none, runs everything inline on the calling thread.
/// </remarks>
internal sealed class OwnerThread
{
    private readonly SynchronizationContext? context;
    private readonly int threadId;

    private OwnerThread(SynchronizationContext? context, int threadId)
    {
        this.context = context;
        this.threadId = threadId;
    }

    /// <summary>Gets a value indicating whether the calling thread is the owner thread.</summary>
    public bool HasAccess => this.context is null || Environment.CurrentManagedThreadId == this.threadId;

    /// <summary>Captures the calling thread as the owner.</summary>
    /// <returns>The owner thread.</returns>
    public static OwnerThread Capture() => new(SynchronizationContext.Current, Environment.CurrentManagedThreadId);

    /// <summary>Runs an action on the owner thread: at once when the caller is on it, later otherwise.</summary>
    /// <param name="action">The action.</param>
    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        if (this.HasAccess)
        {
            action();
            return;
        }

        this.context!.Post(static state => ((Action)state!)(), action);
    }

    /// <summary>Queues an action on the owner thread, even when the caller is on it, so it never runs under the caller's locks.</summary>
    /// <param name="action">The action.</param>
    public void Queue(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        if (this.context is null)
        {
            action();
            return;
        }

        this.context.Post(static state => ((Action)state!)(), action);
    }

    /// <summary>Runs a function on the owner thread and gives its result.</summary>
    /// <remarks>A failure of the function faults the task, whichever thread the caller is on.</remarks>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="function">The function.</param>
    /// <returns>The result.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The function's failure is handed to the caller through the task.")]
    public Task<T> InvokeAsync<T>(Func<T> function)
    {
        ArgumentNullException.ThrowIfNull(function);
        if (this.HasAccess)
        {
            try
            {
                return Task.FromResult(function());
            }
            catch (Exception exception)
            {
                return Task.FromException<T>(exception);
            }
        }

        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.context!.Post(
            _ =>
            {
                try
                {
                    completion.SetResult(function());
                }
                catch (Exception exception)
                {
                    completion.SetException(exception);
                }
            },
            state: null);
        return completion.Task;
    }

    /// <summary>Runs an asynchronous function on the owner thread and completes with it.</summary>
    /// <param name="function">The function.</param>
    /// <returns>The function's completion.</returns>
    public Task InvokeAsync(Func<Task> function)
    {
        ArgumentNullException.ThrowIfNull(function);
        return this.InvokeAsync<Task>(function).Unwrap();
    }

    /// <summary>Runs an asynchronous function on the owner thread and gives its result.</summary>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="function">The function.</param>
    /// <returns>The function's result.</returns>
    public Task<T> InvokeAsync<T>(Func<Task<T>> function)
    {
        ArgumentNullException.ThrowIfNull(function);
        return this.InvokeAsync<Task<T>>(function).Unwrap();
    }
}
