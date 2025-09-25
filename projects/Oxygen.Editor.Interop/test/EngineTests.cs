// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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

        // Run blocking engine loop on a background task (honor cancellation token).
        var runTask = Task.Run(() => this.runner.RunEngine(ctx), token);

        // Let the engine run for some time (cancellable).
        await Task.Delay(TimeSpan.FromMilliseconds(100), token).ConfigureAwait(false);

        // Request shutdown and wait for completion (short grace window), all with ConfigureAwait(false).
        this.runner.StopEngine(ctx);
        var completed = await Task.WhenAny(runTask, Task.Delay(TimeSpan.FromSeconds(2), token)).ConfigureAwait(false);

        Assert.AreSame(runTask, completed, "Engine did not stop within expected timeout after StopEngine");
        Assert.IsTrue(runTask.IsCompleted, "Engine run task should be completed");
    }
}
