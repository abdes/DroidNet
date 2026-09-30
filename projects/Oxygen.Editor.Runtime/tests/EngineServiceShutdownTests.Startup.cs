// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Qualifies native startup acknowledgement before workspace commands are admitted.</summary>
public sealed partial class EngineServiceShutdownTests
{
    /// <summary>Keeps cooked-root operations unavailable until native module registration completes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task StartupWaitsForNativeReadinessBeforeAllowingRootChanges()
    {
        var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = new FakeEngineSession { Startup = ready.Task };
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var startup = service.StartAsync().AsTask();

        _ = service.State.Should().Be(EngineServiceState.Starting);
        _ = startup.IsCompleted.Should().BeFalse();
        var refresh = service.RefreshProjectCookedRootsAsync([]);
        _ = refresh.IsCompleted.Should().BeFalse();
        Mock.Get(native.Commands).Verify(commands => commands.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>()), Times.Never());

        ready.SetResult();
        await startup.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await refresh.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.Running);
        Mock.Get(native.Commands).Verify(commands => commands.ReplaceCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>()), Times.Once());
    }

    /// <summary>Preserves native startup failure and releases partially started ownership.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FailedNativeStartupDoesNotExposeRunning()
    {
        var native = new FakeEngineSession { Startup = Task.FromException(new InvalidOperationException("module registration failed")) };
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await ThrowsAsync<InvalidOperationException>(() => service.StartAsync().AsTask()).ConfigureAwait(false);
        _ = failure.Message.Should().Be("module registration failed");
        _ = native.HasContext.Should().BeFalse();
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    /// <summary>Allows shutdown to cancel a startup wait instead of blocking on the lifecycle gate.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ShutdownDuringStartupCancelsWaitAndStopsLoop()
    {
        var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = new FakeEngineSession { Startup = ready.Task };
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var startup = service.StartAsync().AsTask();
        await service.ShutdownAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> finishStartup = () => startup;
        _ = await finishStartup.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = native.Calls.Should().Contain("Stop");
        _ = native.HasContext.Should().BeFalse();
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }
}
