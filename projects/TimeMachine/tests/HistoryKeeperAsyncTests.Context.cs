// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Specialized;
using AwesomeAssertions;

namespace DroidNet.TimeMachine.Tests;

public partial class HistoryKeeperAsyncTests
{
    /// <summary>
    /// A change that fails or succeeds on another thread still leaves every change of the history's stacks on the
    /// caller's context, where observers such as a view's Undo button live.
    /// </summary>
    /// <param name="fails">Whether the undone change fails.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task UndoAsync_ChangesTheStacksOnTheCallersContext(bool fails)
    {
        using var context = new SingleThreadContext();
        var stackThreads = new ConcurrentBag<int>();

        var callerThread = await context.RunAsync(async () =>
        {
            var history = new HistoryKeeper(new object());
            ((INotifyCollectionChanged)history.UndoStack).CollectionChanged += (_, _) => stackThreads.Add(Environment.CurrentManagedThreadId);
            ((INotifyCollectionChanged)history.RedoStack).CollectionChanged += (_, _) => stackThreads.Add(Environment.CurrentManagedThreadId);
            history.AddChange("rename", async () =>
            {
                await Task.Run(() =>
                {
                    if (fails)
                    {
                        throw new InvalidOperationException("Rejected on another thread.");
                    }
                }).ConfigureAwait(false);
            });

            try
            {
                await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
            }
            catch (InvalidOperationException)
            {
                // The rejection is the case under test; the stacks are checked below.
            }

            return Environment.CurrentManagedThreadId;
        }).ConfigureAwait(false);

        _ = stackThreads.Should().NotBeEmpty().And.OnlyContain(thread => thread == callerThread);
    }

    /// <summary>Runs work on one dedicated thread that is its synchronization context, like a UI thread.</summary>
    private sealed class SingleThreadContext : SynchronizationContext, IDisposable
    {
        private readonly BlockingCollection<(SendOrPostCallback Callback, object? State)> queue = [];
        private readonly Thread thread;

        public SingleThreadContext()
        {
            this.thread = new Thread(() =>
            {
                SetSynchronizationContext(this);
                foreach (var (callback, state) in this.queue.GetConsumingEnumerable())
                {
                    callback(state);
                }
            })
            { IsBackground = true };
            this.thread.Start();
        }

        public override void Post(SendOrPostCallback d, object? state) => this.queue.Add((d, state));

        public Task<T> RunAsync<T>(Func<Task<T>> work)
        {
            var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
            this.Post(
                async _ =>
                {
                    try
                    {
                        completion.SetResult(await work().ConfigureAwait(true));
                    }
                    catch (Exception failure)
                    {
                        _ = completion.TrySetException(failure);
                    }
                },
                state: null);
            return completion.Task;
        }

        public void Dispose()
        {
            this.queue.CompleteAdding();
            this.thread.Join();
            this.queue.Dispose();
        }
    }
}
