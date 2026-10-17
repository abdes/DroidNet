// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport;

/// <summary>
/// A thread with a synchronization context, as the editor's UI thread is: work a test runs there captures it as the
/// owner, and everything posted to it runs there, in order.
/// </summary>
internal sealed partial class OwnerContext : IDisposable
{
    private readonly BlockingCollection<(SendOrPostCallback Callback, object? State)> queue = [];
    private readonly Thread thread;

    /// <summary>Initializes a new instance of the <see cref="OwnerContext"/> class and starts its thread.</summary>
    public OwnerContext()
    {
        this.thread = new Thread(this.Run) { IsBackground = true, Name = "Owner" };
        this.thread.Start();
    }

    /// <summary>Gets the managed id of the owner thread.</summary>
    public int ThreadId => this.thread.ManagedThreadId;

    /// <summary>Runs asynchronous work on the owner thread.</summary>
    /// <param name="work">The work.</param>
    /// <returns>The work's completion; it faults with the work's failure.</returns>
    public Task RunAsync(Func<Task> work) => this.Start(work).Unwrap().Unwrap();

    /// <summary>Runs asynchronous work on the owner thread and gives its result.</summary>
    /// <typeparam name="T">The result type.</typeparam>
    /// <param name="work">The work.</param>
    /// <returns>The work's result; it faults with the work's failure.</returns>
    public Task<T> RunAsync<T>(Func<Task<T>> work) => this.Start(work).Unwrap().Unwrap();

    /// <summary>Waits until everything posted to the owner thread so far has run.</summary>
    /// <returns>The wait.</returns>
    public Task DrainAsync() => this.RunAsync(static () => Task.CompletedTask);

    /// <inheritdoc/>
    public void Dispose()
    {
        this.queue.CompleteAdding();
        _ = this.thread.Join(TimeSpan.FromSeconds(5));
        this.queue.Dispose();
    }

    // Starts the work on the owner thread; a task run synchronously holds whatever the work throws as it starts.
    private Task<Task<TTask>> Start<TTask>(Func<TTask> work)
        where TTask : Task
    {
        var started = new TaskCompletionSource<Task<TTask>>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.queue.Add((
            _ =>
            {
                var run = new Task<TTask>(work);
                run.RunSynchronously();
                started.SetResult(run);
            },
            null));
        return started.Task;
    }

    private void Run()
    {
        SynchronizationContext.SetSynchronizationContext(new Context(this));
        foreach (var (callback, state) in this.queue.GetConsumingEnumerable())
        {
            callback(state);
        }
    }

    private sealed class Context(OwnerContext owner) : SynchronizationContext
    {
        public override void Post(SendOrPostCallback d, object? state)
        {
            if (!owner.queue.IsAddingCompleted)
            {
                owner.queue.Add((d, state));
            }
        }

        public override void Send(SendOrPostCallback d, object? state) => throw new NotSupportedException("The owner thread only takes posts.");

        public override SynchronizationContext CreateCopy() => this;
    }
}
