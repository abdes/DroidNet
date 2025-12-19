// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Interop;

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
    private SynchronizationContext? originalContext;
    private TestSynchronizationContext? uiContext;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public void Setup()
    {
        this.originalContext = SynchronizationContext.Current;
        this.uiContext = null;

        this.runner = new EngineRunner();
        var ok = this.runner.ConfigureLogging(new LoggingConfig
        {
            Verbosity = 1, // INFO / DEBUG
            IsColored = false,
            ModuleOverrides = null,
        });
        _ = ok.Should().BeTrue("Logging configuration failed in test setup");
    }

    [TestCleanup]
    public void Teardown()
    {
        this.runner.Dispose();
        this.runner = null!;
        this.uiContext?.Dispose();
        SynchronizationContext.SetSynchronizationContext(this.originalContext);
        this.uiContext = null;
        this.originalContext = null;
    }

    [TestMethod]
    public void CreateEngine_Succeeds_ReturnsValidContext()
    {
        var cfg = new EngineConfig();
        var ctx = this.CreateEngineUnderTest(cfg);

        _ = ctx.Should().NotBeNull("CreateEngine should return a non-null context once implemented");
        _ = ctx.IsValid.Should().BeTrue("EngineContext should report IsValid = true");
    }

    [TestMethod]
    public void SetTargetFps_InteropAppliesAndClamps()
    {
        var cfg = new EngineConfig();
        var ctx = this.CreateEngineUnderTest(cfg);

        // Apply reasonable fps
        this.runner.SetTargetFps(ctx, 120u);
        var readCfg = this.runner.GetEngineConfig(ctx);
        _ = readCfg.Should().NotBeNull();
        _ = readCfg.TargetFps.Should().Be(120u);

        // Values above allowed maximum are clamped to 240
        this.runner.SetTargetFps(ctx, 10000u);
        readCfg = this.runner.GetEngineConfig(ctx);
        _ = readCfg.TargetFps.Should().Be(240u);

        // 0 means uncapped
        this.runner.SetTargetFps(ctx, 0u);
        readCfg = this.runner.GetEngineConfig(ctx);
        _ = readCfg.TargetFps.Should().Be(0u);
    }

    [TestMethod]
    public void CreateEngine_AfterDispose_ThrowsObjectDisposed()
    {
        this.runner.Dispose();
        var cfg = new EngineConfig();
        var act1 = () => this.runner.CreateEngine(cfg);
        _ = act1.Should().Throw<ObjectDisposedException>();
    }

    [TestMethod]
    public void CreateEngine_WithoutSynchronizationContext_ThrowsInvalidOperation()
    {
        SynchronizationContext.SetSynchronizationContext(syncContext: null);

        try
        {
            var act2 = () => this.runner.CreateEngine(new EngineConfig());
            _ = act2.Should().Throw<InvalidOperationException>();
        }
        finally
        {
            this.RestoreUiContext();
        }
    }

    [TestMethod]
    public async Task RunEngine_RunsForSomeTime_StopsSuccessfully()
    {
        var token = this.TestContext.CancellationToken;

        // Target a modest FPS so frames advance without excess CPU usage.
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.CreateEngineUnderTest(cfg);
        _ = ctx.IsValid.Should().BeTrue();

        // Run the engine loop using the interop-provided background thread helper.
        var runTask = this.runner.RunEngineAsync(ctx);
        await using var registration = token.Register(() => this.runner.StopEngine(ctx)).ConfigureAwait(false);

        // Let the engine run for some time (cancellable).
        await Task.Delay(TimeSpan.FromMilliseconds(100), token).ConfigureAwait(false);

        // Request shutdown and wait for completion (short grace window), all with ConfigureAwait(false).
        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), token)).ConfigureAwait(false);

        _ = runTask.Should().BeSameAs(completed, "Engine did not stop within expected timeout after StopEngine");
        _ = runTask.IsCompleted.Should().BeTrue("Engine run task should be completed");
    }

    [TestMethod]
    public async Task RunEngineAsync_ReturnsNonBlockingTask()
    {
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.CreateEngineUnderTest(cfg);

        var sw = Stopwatch.StartNew();
        var runTask = this.runner.RunEngineAsync(ctx);
        _ = runTask.Should().NotBeNull();
        _ = runTask.IsCompleted.Should().BeFalse("Engine task should not complete synchronously.");
        _ = sw.Elapsed.Should().BeLessThan(TimeSpan.FromMilliseconds(200), "RunEngineAsync should return promptly on the calling thread.");

        await Task.Delay(TimeSpan.FromMilliseconds(100), this.TestContext.CancellationToken).ConfigureAwait(false);

        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);
        _ = runTask.Should().BeSameAs(completed, "Engine did not stop within expected timeout.");
    }

    [TestMethod]
    public async Task RunEngineAsync_DispatchesCleanupViaSynchronizationContext()
    {
        using var syncContext = new TestSynchronizationContext(this.TestContext.CancellationToken);
        var localOriginalContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(syncContext);

        try
        {
            var cfg = new EngineConfig { TargetFps = 30 };
            var ctx = this.runner.CreateEngine(cfg);
            _ = ctx.Should().NotBeNull();

            var runTask = this.runner.RunEngineAsync(ctx);
            _ = runTask.Should().NotBeNull();

            await Task.Delay(TimeSpan.FromMilliseconds(100), this.TestContext.CancellationToken).ConfigureAwait(false);

            this.runner.StopEngine(ctx);
            var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);
            _ = runTask.Should().BeSameAs(completed, "Engine did not stop within timeout.");

            var dispatched = syncContext.TryRunOne(TimeSpan.FromSeconds(1));
            _ = dispatched.Should().BeTrue("Expected cleanup to be posted to synchronization context.");

            syncContext.RunAll();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(localOriginalContext);
        }

        _ = syncContext.PostCount.Should().BePositive("Engine cleanup should post back to the captured synchronization context.");
    }

    [TestMethod]
    public async Task RunEngineAsync_SubsequentCallWhileRunningThrows()
    {
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.CreateEngineUnderTest(cfg);

        var runTask = this.runner.RunEngineAsync(ctx);

        var act3 = () => this.runner.RunEngineAsync(ctx);
        _ = act3.Should().ThrowAsync<InvalidOperationException>();

        await Task.Delay(TimeSpan.FromMilliseconds(100), this.TestContext.CancellationToken).ConfigureAwait(false);

        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);
        _ = runTask.Should().BeSameAs(completed, "Engine should stop after StopEngine.");
    }

    [TestMethod]
    public void RunEngine_WithNullContext_ThrowsArgumentNullException()
    {
        var act5 = () => this.runner.RunEngine(null!);
        _ = act5.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void RunEngineAsync_WithNullContext_ThrowsArgumentNullException()
    {
        var act6 = () => this.runner.RunEngineAsync(null!);
        _ = act6.Should().ThrowAsync<ArgumentNullException>();
    }

    [TestMethod]
    public void RunEngineAsync_AfterDispose_ThrowsObjectDisposedException()
    {
        var ctx = this.CreateEngineUnderTest(new EngineConfig());

        this.runner.Dispose();

        var act7 = () => this.runner.RunEngineAsync(ctx);
        _ = act7.Should().ThrowAsync<ObjectDisposedException>();
    }

    [TestMethod]
    public async Task RegisterSurfaceAsync_WithNullContext_ThrowsArgumentNullException()
    {
        var act8 = async () => await this.runner.TryRegisterSurfaceAsync(
            null!,
            Guid.NewGuid(),
            Guid.NewGuid(),
            "Viewport",
            new nint(1),
            0u,
            0u,
            1.0f).ConfigureAwait(false);

        _ = await act8.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RegisterSurfaceAsync_WithZeroSwapChainPanel_ThrowsArgumentException()
    {
        var ctx = this.CreateEngineUnderTest(new EngineConfig());

        var act9 = async () => await this.runner.TryRegisterSurfaceAsync(
            ctx,
            Guid.NewGuid(),
            Guid.NewGuid(),
            "Viewport",
            nint.Zero,
            0u,
            0u,
            1.0f).ConfigureAwait(false);

        _ = await act9.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RegisterSurfaceAsync_AfterDispose_ThrowsObjectDisposedException()
    {
        var ctx = this.CreateEngineUnderTest(new EngineConfig());

        this.runner.Dispose();

        var act10 = async () => await this.runner.TryRegisterSurfaceAsync(
            ctx,
            Guid.NewGuid(),
            Guid.NewGuid(),
            "Viewport",
            new nint(1),
            0u,
            0u,
            1.0f).ConfigureAwait(false);

        _ = await act10.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RegisterSurfaceAsync_FromNonUiThread_ThrowsInvalidOperationException()
    {
        var ctx = this.CreateEngineUnderTest(new EngineConfig());

        var documentId = Guid.NewGuid();
        var viewportId = Guid.NewGuid();

        var exception = await Task.Run(async () =>
        {
            try
            {
                _ = await this.runner.TryRegisterSurfaceAsync(
                    ctx,
                    documentId,
                    viewportId,
                    "Viewport",
                    new nint(1),
                    0u,
                    0u,
                    1.0f).ConfigureAwait(false);
                return null as InvalidOperationException;
            }
            catch (InvalidOperationException ex)
            {
                return ex;
            }
        }).ConfigureAwait(false);

        _ = exception.Should().NotBeNull("RegisterSurfaceAsync should enforce UI-thread invocation.");
    }

    [TestMethod]
    public async Task RegisterSurfaceAsync_StagedAndResolved_ReturnsTrue()
    {
        var cfg = new EngineConfig { TargetFps = 30 };
        var ctx = this.CreateEngineUnderTest(cfg);

        // Start the engine loop so the engine module will process pending
        // registrations on a frame.
        var runTask = this.runner.RunEngineAsync(ctx);

        var documentId = Guid.NewGuid();
        var viewportId = Guid.NewGuid();

        var regTask = this.runner.TryRegisterSurfaceAsync(ctx, documentId, viewportId, "Viewport", new IntPtr(1), 0u, 0u, 1.0f);

        var completed = await Task.WhenAny(regTask, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);

        _ = regTask.Should().BeSameAs(completed, "RegisterSurfaceAsync did not complete within the expected timeout.");
        _ = (await regTask.ConfigureAwait(false)).Should().BeTrue("RegisterSurfaceAsync should resolve to true when staged and processed by the engine.");

        // Stop the engine loop and wait for complete cleanup.
        this.runner.StopEngine(ctx);
        var stopped = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), this.TestContext.CancellationToken)).ConfigureAwait(false);
        _ = runTask.Should().BeSameAs(stopped, "Engine loop didn't stop within timeout.");
    }

    private void EnsureUiContext()
    {
        this.uiContext ??= new TestSynchronizationContext(this.TestContext.CancellationToken);
        SynchronizationContext.SetSynchronizationContext(this.uiContext);
    }

    private void RestoreUiContext()
    {
        var contextToRestore = (SynchronizationContext?)this.uiContext ?? this.originalContext;
        SynchronizationContext.SetSynchronizationContext(contextToRestore);
    }

    private EngineContext CreateEngineUnderTest(EngineConfig? config = null)
    {
        this.EnsureUiContext();
        var ctx = this.runner.CreateEngine(config ?? new EngineConfig());
        _ = ctx.Should().NotBeNull();
        return ctx;
    }

    private sealed class TestSynchronizationContext(CancellationToken cancellationToken) : SynchronizationContext, IDisposable
    {
        private readonly BlockingCollection<(SendOrPostCallback callback, object? state)> workItems = [];
        private int postCount;

        public int PostCount => Volatile.Read(ref this.postCount);

        public override void Post(SendOrPostCallback d, object? state)
        {
            ArgumentNullException.ThrowIfNull(d);

            this.workItems.Add((d, state), cancellationToken);
            _ = Interlocked.Increment(ref this.postCount);
        }

        public bool TryRunOne(TimeSpan timeout)
        {
            var milliseconds = (int)Math.Min(int.MaxValue, Math.Max(0, timeout.TotalMilliseconds));
            if (!this.workItems.TryTake(out var work, milliseconds, cancellationToken))
            {
                return false;
            }

            work.callback(work.state);
            return true;
        }

        public void RunAll()
        {
            while (this.workItems.TryTake(out var work))
            {
                work.callback(work.state);
            }
        }

        public void Dispose() => this.workItems.Dispose();
    }
}
