// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// A synchronization context pumped by one dedicated thread, like a UI thread: work posted to it runs in order, on
/// that thread, with this context current.
/// </summary>
/// <remarks>
/// Posted work that throws does not end the thread, which would end the whole test run. The context keeps the first
/// such exception, and every later <see cref="RunAsync{T}"/> and <see cref="Send"/> fails with it, so the test that
/// caused it fails.
/// </remarks>
public sealed partial class SingleThreadContext : SynchronizationContext, IDisposable
{
    private readonly BlockingCollection<(SendOrPostCallback callback, object? state)> queue = [];
    private readonly Thread thread;
    private Exception? failure;

    /// <summary>
    /// Initializes a new instance of the <see cref="SingleThreadContext"/> class and starts its thread.
    /// </summary>
    public SingleThreadContext()
    {
        this.thread = new Thread(this.Pump) { IsBackground = true, Name = nameof(SingleThreadContext) };
        this.thread.Start();
    }

    /// <summary>Gets the managed id of the context's thread.</summary>
    public int ThreadId => this.thread.ManagedThreadId;

    /// <summary>Gets a value indicating whether the calling thread is the context's thread.</summary>
    public bool IsCurrentThread => Environment.CurrentManagedThreadId == this.ThreadId;

    /// <summary>Gets the first exception that posted work let escape, if any.</summary>
    public Exception? Failure => Volatile.Read(ref this.failure);

    /// <inheritdoc/>
    public override void Post(SendOrPostCallback d, object? state) => this.queue.Add((d, state));

    /// <inheritdoc/>
    public override void Send(SendOrPostCallback d, object? state)
    {
        ArgumentNullException.ThrowIfNull(d);
        if (this.IsCurrentThread)
        {
            d(state);
            return;
        }

        this.RunAsync(() =>
        {
            d(state);
            return Task.CompletedTask;
        }).GetAwaiter().GetResult();
    }

    /// <inheritdoc/>
    public override SynchronizationContext CreateCopy() => this;

    /// <summary>
    /// Runs work on the context's thread and completes with its result once the work, continuations included, is done.
    /// </summary>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="work">The work.</param>
    /// <returns>The work's result.</returns>
    /// <exception cref="InvalidOperationException">Earlier posted work let an exception escape.</exception>
    public Task<T> RunAsync<T>(Func<Task<T>> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        if (this.Failure is { } earlier)
        {
            return Task.FromException<T>(new InvalidOperationException("Work posted to the test's thread threw.", earlier));
        }

        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.Post(static state => ((Starter<T>)state!).Start(), new Starter<T>(work, completion));
        return completion.Task;
    }

    /// <summary>
    /// Runs work on the context's thread.
    /// </summary>
    /// <param name="work">The work.</param>
    /// <returns>A task that completes when the work is done.</returns>
    public Task RunAsync(Func<Task> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return this.RunAsync(async () =>
        {
            await work().ConfigureAwait(true);
            return true;
        });
    }

    /// <summary>
    /// Stops the thread once the work already posted has run.
    /// </summary>
    public void Dispose()
    {
        this.queue.CompleteAdding();
        if (this.IsCurrentThread)
        {
            // The queue is still being drained when the context is disposed from its own thread.
            return;
        }

        this.thread.Join();
        this.queue.Dispose();
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "an exception escaping the thread would end the test run; it fails the test's next call instead")]
    private void Pump()
    {
        SetSynchronizationContext(this);
        foreach (var (callback, state) in this.queue.GetConsumingEnumerable())
        {
            try
            {
                callback(state);
            }
            catch (Exception error)
            {
                _ = LazyInitializer.EnsureInitialized(ref this.failure, () => error);
            }
        }
    }

    /// <summary>
    /// Starts work on the context's thread and forwards its outcome to the caller's task.
    /// </summary>
    private sealed class Starter<T>(Func<Task<T>> work, TaskCompletionSource<T> completion)
    {
        [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the exception is forwarded to the caller's task")]
        public void Start()
        {
            Task<T> started;
            try
            {
                started = work();
            }
            catch (Exception error)
            {
                _ = completion.TrySetException(error);
                return;
            }

            _ = started.ContinueWith(
                static (task, state) => ((TaskCompletionSource<T>)state!).TrySetFromTask(task),
                completion,
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }
    }
}
