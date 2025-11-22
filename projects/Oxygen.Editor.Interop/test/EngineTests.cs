// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.EngineInterface;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[SuppressMessage(
    "Design",
    "CA1001:Types that own disposable fields should be disposable",
    Justification = "MSTest lifecycle takes care of that")]
[TestCategory(nameof(EngineRunner))]
public sealed class EngineTests
{
    private EngineRunner runner = null!; // Initialized in TestInitialize

    public TestContext? TestContext { get; set; }

    [TestInitialize]
    public void Setup()
    {
        this.runner = new EngineRunner();
        var ok = this.runner.ConfigureLogging(new LoggingConfig
        {
            Verbosity = 1, // INFO / DEBUG
            IsColored = false,
            ModuleOverrides = null,
        });
        Assert.IsTrue(ok, "Logging configuration failed in test setup");
    }

    [TestCleanup]
    public void Teardown()
    {
        this.runner.Dispose();
        this.runner = null!;
    }

    [TestMethod]
    public void CreateEngine_Succeeds_ReturnsValidContext()
    {
        var cfg = new EngineConfig();
        var ctx = this.runner.CreateEngine(cfg);

        Assert.IsNotNull(ctx, "CreateEngine should return a non-null context once implemented");
        Assert.IsTrue(ctx.IsValid, "EngineContext should report IsValid = true");
    }

    [TestMethod]
    public void CreateEngine_AfterDispose_ThrowsObjectDisposed()
    {
        this.runner.Dispose();
        var cfg = new EngineConfig();
        _ = Assert.ThrowsExactly<ObjectDisposedException>(() => this.runner.CreateEngine(cfg));
    }

    [TestMethod]
    public async Task RunEngine_RunsForSomeTime_StopsSuccessfully()
    {
        var token = this.TestContext?.CancellationTokenSource.Token ?? CancellationToken.None;

        // Target a modest FPS so frames advance without excess CPU usage.
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.runner.CreateEngine(cfg);
        Assert.IsNotNull(ctx);
        Assert.IsTrue(ctx.IsValid);

        // Run the engine loop using the interop-provided background thread helper.
        var runTask = this.runner.RunEngineAsync(ctx);
        using var registration = token.Register(() => this.runner.StopEngine(ctx));

        // Let the engine run for some time (cancellable).
        await Task.Delay(TimeSpan.FromMilliseconds(100), token).ConfigureAwait(false);

        // Request shutdown and wait for completion (short grace window), all with ConfigureAwait(false).
        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), token)).ConfigureAwait(false);

        Assert.AreSame(runTask, completed, "Engine did not stop within expected timeout after StopEngine");
        Assert.IsTrue(runTask.IsCompleted, "Engine run task should be completed");
    }

    [TestMethod]
    public async Task RunEngineAsync_ReturnsNonBlockingTask()
    {
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.runner.CreateEngine(cfg);
        Assert.IsNotNull(ctx);

        var sw = Stopwatch.StartNew();
        var runTask = this.runner.RunEngineAsync(ctx);
        Assert.IsNotNull(runTask);
        Assert.IsFalse(runTask.IsCompleted, "Engine task should not complete synchronously.");
        Assert.IsTrue(sw.Elapsed < TimeSpan.FromMilliseconds(200),
            "RunEngineAsync should return promptly on the calling thread.");

        await Task.Delay(TimeSpan.FromMilliseconds(100)).ConfigureAwait(false);

        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2))).ConfigureAwait(false);
        Assert.AreSame(runTask, completed, "Engine did not stop within expected timeout.");
    }

    [TestMethod]
    public async Task RunEngineAsync_DispatchesCleanupViaSynchronizationContext()
    {
        using var syncContext = new TestSynchronizationContext();
        var originalContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(syncContext);

        try
        {
            var cfg = new EngineConfig { TargetFps = 30 };
            var ctx = this.runner.CreateEngine(cfg);
            Assert.IsNotNull(ctx);

            var runTask = this.runner.RunEngineAsync(ctx);
            Assert.IsNotNull(runTask);

            await Task.Delay(TimeSpan.FromMilliseconds(100)).ConfigureAwait(false);

            this.runner.StopEngine(ctx);
            var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2))).ConfigureAwait(false);
            Assert.AreSame(runTask, completed, "Engine did not stop within timeout.");

            var dispatched = syncContext.TryRunOne(TimeSpan.FromSeconds(1));
            Assert.IsTrue(dispatched, "Expected cleanup to be posted to synchronization context.");

            syncContext.RunAll();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(originalContext);
        }

        Assert.IsTrue(syncContext.PostCount > 0,
            "Engine cleanup should post back to the captured synchronization context.");
    }

    [TestMethod]
    public async Task RunEngineAsync_SubsequentCallWhileRunningThrows()
    {
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.runner.CreateEngine(cfg);
        Assert.IsNotNull(ctx);

        var runTask = this.runner.RunEngineAsync(ctx);

        _ = Assert.ThrowsExactly<InvalidOperationException>(() => this.runner.RunEngineAsync(ctx));

        await Task.Delay(TimeSpan.FromMilliseconds(100)).ConfigureAwait(false);

        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2))).ConfigureAwait(false);
        Assert.AreSame(runTask, completed, "Engine should stop after StopEngine.");
    }

    private sealed class TestSynchronizationContext : SynchronizationContext, IDisposable
    {
        private readonly BlockingCollection<(SendOrPostCallback Callback, object? State)> workItems = new();
        private int postCount;

        public int PostCount => Volatile.Read(ref this.postCount);

        public override void Post(SendOrPostCallback d, object? state)
        {
            if (d == null)
            {
                throw new ArgumentNullException(nameof(d));
            }

            this.workItems.Add((d, state));
            Interlocked.Increment(ref this.postCount);
        }

        public bool TryRunOne(TimeSpan timeout)
        {
            var milliseconds = (int)Math.Min(int.MaxValue, Math.Max(0, timeout.TotalMilliseconds));
            if (!this.workItems.TryTake(out var work, milliseconds))
            {
                return false;
            }

            work.Callback(work.State);
            return true;
        }

        public void RunAll()
        {
            while (this.workItems.TryTake(out var work))
            {
                work.Callback(work.State);
            }
        }

        public void Dispose()
        {
            this.workItems.Dispose();
        }
    }
}
