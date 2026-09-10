// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
[DoNotParallelize]
public sealed partial class NativeLoopCleanupTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [SuppressMessage("Reliability", "CA2025:Ensure tasks using IDisposable instances complete before the instances are disposed", Justification = "The loop lifetime must overlap StopEngine; finally awaits both the loop and UI cleanup before context and runner disposal.")]
    public async Task CleanupCompletion_IncludesPostedNativeCleanup()
    {
        var dispatcher = new QueuedContext();
        var previous = SynchronizationContext.Current;
        using var runner = new EngineRunner();
        EngineContext context;
        try
        {
            SynchronizationContext.SetSynchronizationContext(dispatcher);
            context = runner.CreateEngine(new EngineConfig
            {
                TargetFps = 30,
                EnableAssetLoader = true,
                Graphics = new GraphicsConfigManaged { Headless = true },
            });
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }

        using (context)
        {
            var loop = runner.RunEngineAsync(context);
            var cleanup = runner.WaitForLoopCleanupAsync();
            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(100), this.TestContext.CancellationToken).ConfigureAwait(false);
                runner.StopEngine(context);
                await loop.ConfigureAwait(false);
                await dispatcher.Posted.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
                _ = cleanup.IsCompleted.Should().BeFalse();
            }
            finally
            {
                runner.StopEngine(context);
                await loop.ConfigureAwait(false);
                await dispatcher.Posted.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
                dispatcher.Drain();
                await cleanup.ConfigureAwait(false);
            }
        }
    }

    private sealed class QueuedContext : SynchronizationContext
    {
        private readonly ConcurrentQueue<(SendOrPostCallback callback, object? state)> callbacks = new();

        public TaskCompletionSource Posted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public override void Post(SendOrPostCallback d, object? state)
        {
            this.callbacks.Enqueue((d, state));
            _ = this.Posted.TrySetResult();
        }

        public void Drain()
        {
            while (this.callbacks.TryDequeue(out var callback))
            {
                callback.callback(callback.state);
            }
        }
    }
}
