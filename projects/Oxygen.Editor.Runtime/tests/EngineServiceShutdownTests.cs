// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed partial class EngineServiceShutdownTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task StartupFailure_ReleasesContextAndPreservesTheCause()
    {
        var native = new FakeEngineSession { FailAt = "Run" };
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await ThrowsAsync<InvalidOperationException>(() => service.StartAsync().AsTask()).ConfigureAwait(false);
        _ = failure.Message.Should().Be("Run");
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task FaultedLoop_IsVisibleBeforeShutdownAndCleanupStillCompletes()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        native.Loop.SetException(new InvalidOperationException("Native failure"));
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task FailedDispatcherCleanup_RetainsOwnershipUntilItCanComplete()
    {
        var native = new FakeEngineSession { FailAt = "Loop cleanup" };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = native.HasContext.Should().BeTrue();
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        native.FailAt = null;
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task DisposalFailure_IsLogged()
    {
        var logger = new Mock<ILogger>();
        _ = logger.Setup(value => value.IsEnabled(It.IsAny<LogLevel>())).Returns(value: true);
        var factory = new Mock<ILoggerFactory>();
        _ = factory.Setup(value => value.CreateLogger(It.IsAny<string>())).Returns(logger.Object);
        var native = new FakeEngineSession { FailAt = "Stop" };
        var service = new EngineService(Context(), () => native, Mock.Of<IOperationResultPublisher>(), loggerFactory: factory.Object);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);
        await service.DisposeAsync().ConfigureAwait(false);
        logger.Verify(
            value => value.Log(
                LogLevel.Error,
                It.IsAny<EventId>(),
                It.IsAny<It.IsAnyType>(),
                It.Is<Exception>(exception => string.Equals(exception.Message, "Stop", StringComparison.Ordinal)),
                It.IsAny<Func<It.IsAnyType, Exception?, string>>()),
            Times.Once());
        native.FailAt = null;
        await service.DisposeAsync().ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(false, 0)]
    [DataRow(true, 0)]
    [DataRow(true, 3)]
    public async Task Shutdown_ReleasesOwnedResources(bool start, int leaseCount)
    {
        var native = new FakeEngineSession();
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.Ready);
        if (start)
        {
            await service.StartAsync().ConfigureAwait(false);
        }

        for (var i = 0; i < leaseCount; ++i)
        {
            _ = await AttachAsync(service).ConfigureAwait(false);
        }

        await service.ShutdownAsync().ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasContext.Should().BeFalse();
        _ = native.HasRunner.Should().BeFalse();
        _ = native.Calls.Count(call => string.Equals(call, "Unregister", StringComparison.Ordinal)).Should().Be(leaseCount);
        if (start)
        {
            _ = native.Calls.IndexOf("Loop cleanup").Should().BeLessThan(native.Calls.IndexOf("Destroy context"));
        }
    }

    [TestMethod]
    [DataRow("Create runner")]
    [DataRow("Create context")]
    public async Task InitializeFailure_CleansPartialOwnershipAndPreservesFailure(string stage)
    {
        var native = new FakeEngineSession { FailAt = stage };
        var service = Create(native);
        await using var lifetime = service.ConfigureAwait(false);
        var failure = await ThrowsAsync<InvalidOperationException>(() => service.InitializeAsync(this.TestContext.CancellationToken).AsTask()).ConfigureAwait(false);
        _ = failure.Message.Should().Be(stage);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasRunner.Should().BeFalse();
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task FailedLease_DoesNotSkipRemainingLeasesOrNativeOwners()
    {
        var native = new FakeEngineSession();
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var failed = await AttachAsync(service).ConfigureAwait(false);
        var other = await AttachAsync(service).ConfigureAwait(false);
        native.Unregister = id => id == failed.Key.ViewportId ? throw new InvalidOperationException("Surface failure") : Task.FromResult(true);

        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = native.Calls.Count(call => string.Equals(call, "Unregister", StringComparison.Ordinal)).Should().Be(2);
        _ = failed.IsAttached.Should().BeFalse();
        _ = other.IsAttached.Should().BeFalse();
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    [TestMethod]
    public async Task StopFailure_RetainsLiveContextAndCanRetry()
    {
        var native = new FakeEngineSession { FailAt = "Stop" };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        _ = native.HasContext.Should().BeTrue();
        _ = native.Calls.Should().NotContain("Destroy context");
        _ = await ThrowsAsync<AggregateException>(() => service.InitializeAsync(this.TestContext.CancellationToken).AsTask()).ConfigureAwait(false);
        native.FailAt = null;
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    [TestMethod]
    [DataRow("Destroy runner")]
    [DataRow("Destroy context")]
    public async Task DestructionFailure_RemainsOwnedUntilRetry(string stage)
    {
        var native = new FakeEngineSession { FailAt = stage };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        native.FailAt = null;
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    [TestMethod]
    public async Task DisposeFailure_IsNonThrowingAndDoesNotPreventCleanupRetry()
    {
        var native = new FakeEngineSession { FailAt = "Stop" };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await service.DisposeAsync().ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        _ = await ThrowsAsync<ObjectDisposedException>(() => service.InitializeAsync(this.TestContext.CancellationToken).AsTask()).ConfigureAwait(false);
        native.FailAt = null;
        await service.DisposeAsync().ConfigureAwait(false);
        await service.DisposeAsync().ConfigureAwait(false);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.Calls.Count(call => string.Equals(call, "Destroy runner", StringComparison.Ordinal)).Should().Be(1);
    }

    [TestMethod]
    public async Task Shutdown_WaitsForLoopAndDispatcherCleanupBeforeDestroyingContext()
    {
        var cleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = new FakeEngineSession { CompleteOnStop = false, Cleanup = cleanup.Task };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var shutdown = service.ShutdownAsync().AsTask();
        var repeated = service.ShutdownAsync().AsTask();
        _ = shutdown.IsCompleted.Should().BeFalse();
        _ = native.HasContext.Should().BeTrue();
        native.Loop.SetResult();
        _ = native.HasContext.Should().BeTrue();
        cleanup.SetResult();
        await Task.WhenAll(shutdown, repeated).ConfigureAwait(false);
        _ = native.Calls.Count(call => string.Equals(call, "Destroy context", StringComparison.Ordinal)).Should().Be(1);
    }

    [TestMethod]
    public async Task LoopFaultDuringRelease_DoesNotWaitForUnserviceableRemoval()
    {
        var removal = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = new FakeEngineSession { Unregister = _ => removal.Task };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await AttachAsync(service).ConfigureAwait(false);
        var shutdown = service.ShutdownAsync().AsTask();
        native.Loop.SetException(new InvalidOperationException("Loop failed"));
        _ = await ThrowsAsync<AggregateException>(() => shutdown.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken)).ConfigureAwait(false);
        removal.SetResult(false);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    [TestMethod]
    public async Task Shutdown_DrainsAnInFlightRegistration()
    {
        var registration = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var native = new FakeEngineSession { Register = _ => registration.Task };
        var service = await this.StartAsync(native).ConfigureAwait(false);
        await using var lifetime = service.ConfigureAwait(false);
        var attach = AttachAsync(service);
        var shutdown = service.ShutdownAsync().AsTask();
        _ = shutdown.IsCompleted.Should().BeFalse();
        registration.SetResult(true);
        var lease = await attach.ConfigureAwait(false);
        await shutdown.ConfigureAwait(false);
        _ = lease.IsAttached.Should().BeFalse();
        _ = native.Calls.Should().ContainInOrder("Register", "Unregister", "Stop", "Loop cleanup", "Destroy runner", "Destroy context");
    }

    [TestMethod]
    public async Task Restart_ClearsFailedReservationsAndIgnoresOldLeaseDisposal()
    {
        var first = new FakeEngineSession { Unregister = _ => Task.FromResult(false) };
        var second = new FakeEngineSession();
        var sessions = new Queue<EngineSession>([first, second]);
        var service = new EngineService(Context(), sessions.Dequeue, Mock.Of<IOperationResultPublisher>());
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);
        var oldLease = await AttachAsync(service).ConfigureAwait(false);
        _ = await ThrowsAsync<AggregateException>(() => service.ShutdownAsync().AsTask()).ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);
        _ = await service.AttachViewportCoreAsync(oldLease.Key, null!, this.TestContext.CancellationToken).ConfigureAwait(false);
        await oldLease.DisposeAsync().ConfigureAwait(false);
        _ = service.ActiveSurfaceCount.Should().Be(1);
    }

    private static async Task<T> ThrowsAsync<T>(Func<Task> action)
        where T : Exception
    {
        var assertion = await action.Should().ThrowExactlyAsync<T>().ConfigureAwait(false);
        return assertion.Which;
    }

    private static HostingContext Context() => new() { Dispatcher = null!, Application = null!, DispatcherScheduler = null! };

    private static EngineService Create(FakeEngineSession native) => new(Context(), () => native, Mock.Of<IOperationResultPublisher>());

    private static Task<IViewportSurfaceLease> AttachAsync(EngineService service)
        => service.AttachViewportCoreAsync(new ViewportSurfaceKey(Guid.NewGuid(), Guid.NewGuid()), null!).AsTask();

    private async Task<EngineService> StartAsync(FakeEngineSession native)
    {
        var service = Create(native);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);
        return service;
    }
}
