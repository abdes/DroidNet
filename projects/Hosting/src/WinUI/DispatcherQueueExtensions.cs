// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Microsoft.UI.Dispatching;

namespace DroidNet.Hosting.WinUI;

/// <summary>
/// Provides extension methods for the <see cref="DispatcherQueue"/> to safely execute actions on the UI thread.
/// </summary>
/// <remarks>
/// <para>
/// These internal implementations provide a robust way to marshal work to the UI thread while handling common
/// edge cases like re-entrancy, task proxying, and dispatcher shutdown.
/// </para>
/// <para>
/// <strong>Note for Maintainers:</strong>
/// <list type="bullet">
/// <item>Always check <c>HasThreadAccess</c> before enqueuing to avoid unnecessary context switches and potential deadlocks.</item>
/// <item>Use <c>Task.CompletedTask</c> or <c>Task.FromResult</c> on the synchronous path to minimize heap allocations.</item>
/// <item>When enqueuing <c>Func&lt;Task&gt;</c>, the inner task is awaited with <c>ConfigureAwait(false)</c> inside the dispatcher
/// loop, but the outer task completes only when the inner one is done.</item>
/// </list>
/// </para>
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "designed to catch all exceptions")]
public static class DispatcherQueueExtensions
{
    /// <summary>
    /// Invokes a given function on the target <see cref="DispatcherQueue"/> and returns a
    /// <see cref="Task"/> that completes when the invocation of the function is completed.
    /// </summary>
    /// <param name="dispatcher">The target <see cref="DispatcherQueue"/> to invoke the code on.</param>
    /// <param name="function">The <see cref="Action"/> to invoke.</param>
    /// <param name="priority">The priority level for the function to invoke.</param>
    /// <returns>A <see cref="Task"/> that completes when the invocation of <paramref name="function"/> is over.</returns>
    /// <remarks>If the current thread has access to <paramref name="dispatcher"/>, <paramref name="function"/> will be invoked directly.</remarks>
    public static Task DispatchAsync(this DispatcherQueue dispatcher, Action function, DispatcherQueuePriority priority = DispatcherQueuePriority.Normal)
    {
        if (dispatcher.HasThreadAccess)
        {
            try
            {
                function();

                // Returning a cached completed task avoids a new allocation on every synchronous hit.
                return Task.CompletedTask;
            }
            catch (Exception e)
            {
                return Task.FromException(e);
            }
        }

        return TryDispatchAsync(dispatcher, function, priority);
    }

    /// <summary>
    /// Invokes a given function on the target <see cref="DispatcherQueue"/> and returns a
    /// <see cref="Task{TResult}"/> that completes when the invocation of the function is completed.
    /// </summary>
    /// <typeparam name="T">The return type of <paramref name="function"/> to relay through the returned <see cref="Task{TResult}"/>.</typeparam>
    /// <param name="dispatcher">The target <see cref="DispatcherQueue"/> to invoke the code on.</param>
    /// <param name="function">The <see cref="Func{TResult}"/> to invoke.</param>
    /// <param name="priority">The priority level for the function to invoke.</param>
    /// <returns>A <see cref="Task"/> that completes when the invocation of <paramref name="function"/> is over.</returns>
    /// <remarks>If the current thread has access to <paramref name="dispatcher"/>, <paramref name="function"/> will be invoked directly.</remarks>
    public static Task<T> DispatchAsync<T>(this DispatcherQueue dispatcher, Func<T> function, DispatcherQueuePriority priority = DispatcherQueuePriority.Normal)
    {
        if (dispatcher.HasThreadAccess)
        {
            try
            {
                return Task.FromResult(function());
            }
            catch (Exception e)
            {
                return Task.FromException<T>(e);
            }
        }

        return TryDispatchAsync(dispatcher, function, priority);
    }

    /// <summary>
    /// Invokes a given function on the target <see cref="DispatcherQueue"/> and returns a
    /// <see cref="Task"/> that acts as a proxy for the one returned by the given function.
    /// </summary>
    /// <param name="dispatcher">The target <see cref="DispatcherQueue"/> to invoke the code on.</param>
    /// <param name="function">The <see cref="Func{TResult}"/> to invoke.</param>
    /// <param name="priority">The priority level for the function to invoke.</param>
    /// <returns>A <see cref="Task"/> that acts as a proxy for the one returned by <paramref name="function"/>.</returns>
    /// <remarks>
    /// If the current thread has access to <paramref name="dispatcher"/>, <paramref name="function"/> will be invoked directly.
    /// The returned task will represent the completion of the task returned by <paramref name="function"/>.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Roslynator", "RCS1229:Use async/await when necessary", Justification = "avoiding state machine overhead for direct task proxying")]
    public static Task DispatchAsync(this DispatcherQueue dispatcher, Func<Task> function, DispatcherQueuePriority priority = DispatcherQueuePriority.Normal)
    {
        if (dispatcher.HasThreadAccess)
        {
            try
            {
                // Proxy the task directly to avoid wrapping overhead when already on the UI thread.
                return function() ?? Task.FromException(GetEnqueueException("The Task returned by function cannot be null."));
            }
            catch (Exception e)
            {
                return Task.FromException(e);
            }
        }

        return TryDispatchAsync(dispatcher, function, priority);
    }

    /// <summary>
    /// Invokes a given function on the target <see cref="DispatcherQueue"/> and returns a
    /// <see cref="Task{TResult}"/> that acts as a proxy for the one returned by the given function.
    /// </summary>
    /// <typeparam name="T">The return type of <paramref name="function"/> to relay through the returned <see cref="Task{TResult}"/>.</typeparam>
    /// <param name="dispatcher">The target <see cref="DispatcherQueue"/> to invoke the code on.</param>
    /// <param name="function">The <see cref="Func{TResult}"/> to invoke.</param>
    /// <param name="priority">The priority level for the function to invoke.</param>
    /// <returns>A <see cref="Task{TResult}"/> that relays the one returned by <paramref name="function"/>.</returns>
    /// <remarks>If the current thread has access to <paramref name="dispatcher"/>, <paramref name="function"/> will be invoked directly.</remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Roslynator", "RCS1229:Use async/await when necessary", Justification = "avoiding state machine overhead for direct task proxying")]
    public static Task<T> DispatchAsync<T>(this DispatcherQueue dispatcher, Func<Task<T>> function, DispatcherQueuePriority priority = DispatcherQueuePriority.Normal)
    {
        if (dispatcher.HasThreadAccess)
        {
            try
            {
                // Proxy the task directly to avoid wrapping overhead when already on the UI thread.
                return function() ?? Task.FromException<T>(GetEnqueueException("The Task returned by function cannot be null."));
            }
            catch (Exception e)
            {
                return Task.FromException<T>(e);
            }
        }

        return TryDispatchAsync(dispatcher, function, priority);
    }

    private static Task TryDispatchAsync(DispatcherQueue dispatcher, Action function, DispatcherQueuePriority priority)
    {
        var taskCompletionSource = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        // TryEnqueue returns false if the dispatcher is shutting down.
        // We capture this and surface it as an exception in the returned task.
        if (!dispatcher.TryEnqueue(priority, () =>
            {
                try
                {
                    function();
                    taskCompletionSource.SetResult();
                }
                catch (Exception e)
                {
                    taskCompletionSource.SetException(e);
                }
            }))
        {
            taskCompletionSource.SetException(GetEnqueueException("Failed to enqueue the operation. The dispatcher queue may be shutting down."));
        }

        return taskCompletionSource.Task;
    }

    private static Task<T> TryDispatchAsync<T>(DispatcherQueue dispatcher, Func<T> function, DispatcherQueuePriority priority)
    {
        var taskCompletionSource = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);

        if (!dispatcher.TryEnqueue(priority, () =>
            {
                try
                {
                    taskCompletionSource.SetResult(function());
                }
                catch (Exception e)
                {
                    taskCompletionSource.SetException(e);
                }
            }))
        {
            taskCompletionSource.SetException(GetEnqueueException("Failed to enqueue the operation. The dispatcher queue may be shutting down."));
        }

        return taskCompletionSource.Task;
    }

    private static Task TryDispatchAsync(DispatcherQueue dispatcher, Func<Task> function, DispatcherQueuePriority priority)
    {
        var taskCompletionSource = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        if (!dispatcher.TryEnqueue(priority, () =>
            {
                try
                {
                    var task = function();
                    if (task is not null)
                    {
                        // We attach a continuation to the inner task to ensure the outer TCS only completes
                        // when the actual work is done, avoiding async void.
                        _ = task.ContinueWith(
                            t =>
                            {
                                if (t.IsFaulted)
                                {
                                    taskCompletionSource.SetException(t.Exception!.InnerExceptions);
                                }
                                else if (t.IsCanceled)
                                {
                                    taskCompletionSource.SetCanceled();
                                }
                                else
                                {
                                    taskCompletionSource.SetResult();
                                }
                            },
                            CancellationToken.None,
                            TaskContinuationOptions.ExecuteSynchronously,
                            TaskScheduler.Default);
                    }
                    else
                    {
                        taskCompletionSource.SetException(GetEnqueueException("The Task returned by function cannot be null."));
                    }
                }
                catch (Exception e)
                {
                    taskCompletionSource.SetException(e);
                }
            }))
        {
            taskCompletionSource.SetException(GetEnqueueException("Failed to enqueue the operation. The dispatcher queue may be shutting down."));
        }

        return taskCompletionSource.Task;
    }

    private static Task<T> TryDispatchAsync<T>(DispatcherQueue dispatcher, Func<Task<T>> function, DispatcherQueuePriority priority)
    {
        var taskCompletionSource = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);

        if (!dispatcher.TryEnqueue(priority, () =>
            {
                try
                {
                    var task = function();
                    if (task is not null)
                    {
                        _ = task.ContinueWith(
                            t =>
                            {
                                if (t.IsFaulted)
                                {
                                    taskCompletionSource.SetException(t.Exception!.InnerExceptions);
                                }
                                else if (t.IsCanceled)
                                {
                                    taskCompletionSource.SetCanceled();
                                }
                                else
                                {
                                    taskCompletionSource.SetResult(t.Result);
                                }
                            },
                            CancellationToken.None,
                            TaskContinuationOptions.ExecuteSynchronously,
                            TaskScheduler.Default);
                    }
                    else
                    {
                        taskCompletionSource.SetException(GetEnqueueException("The Task returned by function cannot be null."));
                    }
                }
                catch (Exception e)
                {
                    taskCompletionSource.SetException(e);
                }
            }))
        {
            taskCompletionSource.SetException(GetEnqueueException("Failed to enqueue the operation. The dispatcher queue may be shutting down."));
        }

        return taskCompletionSource.Task;
    }

    /// <summary>
    /// Helper to create a consistent exception for enqueue failures.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static InvalidOperationException GetEnqueueException(string message)
        => new(message);
}
