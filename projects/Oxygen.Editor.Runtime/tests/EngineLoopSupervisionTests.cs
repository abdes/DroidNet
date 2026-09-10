// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class EngineLoopSupervisionTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task UnexpectedCompletion_PublishesWithoutPollingStateAndEndsPendingWork(bool fault)
    {
        var native = new FakeEngineSession();
        var results = new ConcurrentQueue<OperationResult>();
        var publisher = Publisher(results);
        var service = Create(() => native, publisher.Object);
        await using var lifetime = service.ConfigureAwait(false);
        var notification = FaultNotification(service);
        await this.StartAsync(service).ConfigureAwait(false);
        var runId = service.WorldCommands.RunId;
        var activation = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = Mock.Get(native.Commands).Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).Returns(activation.Task);
        var target = new RuntimeSceneTarget(runId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        var pending = service.WorldCommands.ActivateSceneAsync(Guid.NewGuid(), target, "Pending", this.TestContext.CancellationToken);
        var original = new InvalidOperationException("Native frame failed", new IOException("Original cause"));

        if (fault)
        {
            native.Loop.SetException(original);
        }
        else
        {
            native.Loop.SetResult();
        }

        var change = await notification.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var command = await pending.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = change.RunId.Should().Be(runId);
        _ = change.State.Should().Be(EngineServiceState.Faulted);
        _ = change.Exception.Should().BeSameAs(fault ? original : null);
        _ = service.State.Should().Be(EngineServiceState.Faulted);
        _ = native.HasContext.Should().BeTrue("observation must preserve native cleanup ownership");
        _ = command.Status.Should().Be(RuntimeCommandStatus.Unavailable);
        var result = results.Should().ContainSingle().Which;
        _ = result.OperationId.Should().Be(runId);
        _ = result.OperationKind.Should().Be(RuntimeOperationKinds.Loop);
        _ = result.Should().BeSameAs(change.OperationResult);
        _ = result.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(fault ? RuntimeDiagnosticCodes.LoopFaulted : RuntimeDiagnosticCodes.LoopExited);
        if (fault)
        {
            _ = result.Diagnostics[0].TechnicalMessage.Should().Contain("Original cause");
        }
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RequestedStop_DoesNotPublishSpuriousFault(bool cancellation)
    {
        var native = new FakeEngineSession { CompleteOnStop = !cancellation };
        var results = new ConcurrentQueue<OperationResult>();
        var service = Create(() => native, Publisher(results).Object);
        await using var lifetime = service.ConfigureAwait(false);
        var changes = new ConcurrentQueue<EngineStateChangedEventArgs>();
        service.StateChanged += (_, change) => changes.Enqueue(change);
        await this.StartAsync(service).ConfigureAwait(false);

        var shutdown = service.ShutdownAsync().AsTask();
        if (cancellation)
        {
            native.Loop.SetCanceled(this.TestContext.CancellationToken);
        }

        await shutdown.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = results.Should().BeEmpty();
        _ = changes.Should().NotContain(change => change.State == EngineServiceState.Faulted);
        _ = changes.Select(change => change.State).Should().ContainInOrder(EngineServiceState.Running, EngineServiceState.ShuttingDown, EngineServiceState.NoEngine);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task ExitBeforeShutdown_IsReportedExactlyOnce()
    {
        var native = new FakeEngineSession();
        var results = new ConcurrentQueue<OperationResult>();
        var service = Create(() => native, Publisher(results).Object);
        await using var lifetime = service.ConfigureAwait(false);
        var notification = FaultNotification(service);
        await this.StartAsync(service).ConfigureAwait(false);

        native.Loop.SetResult();
        await service.ShutdownAsync().ConfigureAwait(false);
        _ = await notification.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = results.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(RuntimeDiagnosticCodes.LoopExited);
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
    }

    [TestMethod]
    public async Task FaultDuringShutdown_PreservesOriginalFailureAndReleasesOwnership()
    {
        var native = new FakeEngineSession { CompleteOnStop = false };
        var results = new ConcurrentQueue<OperationResult>();
        var service = Create(() => native, Publisher(results).Object);
        await using var lifetime = service.ConfigureAwait(false);
        var notification = FaultNotification(service);
        await this.StartAsync(service).ConfigureAwait(false);
        var original = new InvalidOperationException("Fault after stop requested");

        var shutdown = service.ShutdownAsync().AsTask();
        native.Loop.SetException(original);
        var waitForShutdown = () => shutdown.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken);
        var failure = await waitForShutdown.Should().ThrowExactlyAsync<AggregateException>().ConfigureAwait(false);
        var change = await notification.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = failure.Which.InnerExceptions.Should().Contain(original);
        _ = change.Exception.Should().BeSameAs(original);
        _ = results.Should().ContainSingle();
        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasContext.Should().BeFalse();
    }

    [TestMethod]
    public async Task RestartInsideOldNotification_DoesNotLetOldObserverOverwriteNewRun()
    {
        var first = new FakeEngineSession();
        var second = new FakeEngineSession();
        var sessions = new Queue<EngineSession>([first, second]);
        var results = new ConcurrentQueue<OperationResult>();
        var service = Create(sessions.Dequeue, Publisher(results).Object);
        await using var lifetime = service.ConfigureAwait(false);
        await this.StartAsync(service).ConfigureAwait(false);
        var oldRun = service.WorldCommands.RunId;
        var restarting = new TaskCompletionSource<Task>(TaskCreationOptions.RunContinuationsAsynchronously);
        service.StateChanged += (_, change) =>
        {
            if (change.RunId == oldRun && change.OperationResult is not null)
            {
                restarting.SetResult(this.RestartAsync(service));
            }
        };

        first.Loop.SetResult();
        var restart = await restarting.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await restart.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = service.State.Should().Be(EngineServiceState.Running);
        _ = service.WorldCommands.RunId.Should().NotBeEmpty().And.NotBe(oldRun);
        _ = second.HasContext.Should().BeTrue();
        _ = first.HasContext.Should().BeFalse();
        _ = results.Should().ContainSingle().Which.OperationId.Should().Be(oldRun);
    }

    [TestMethod]
    public async Task ImmediateLoopExit_HasTheSameNonemptyIdentityAsRunningNotification()
    {
        var native = new FakeEngineSession();
        native.Loop.SetResult();
        var service = Create(() => native, Mock.Of<IOperationResultPublisher>());
        await using var lifetime = service.ConfigureAwait(false);
        var notification = FaultNotification(service);
        var running = new TaskCompletionSource<Guid>(TaskCreationOptions.RunContinuationsAsynchronously);
        service.StateChanged += (_, change) =>
        {
            if (change.State == EngineServiceState.Running)
            {
                running.SetResult(change.RunId);
            }
        };

        await this.StartAsync(service).ConfigureAwait(false);
        var change = await notification.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var startedRun = await running.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = startedRun.Should().NotBeEmpty();
        _ = change.RunId.Should().Be(startedRun);
        _ = change.OperationResult!.OperationId.Should().Be(startedRun);
    }

    [TestMethod]
    public async Task FailingSubscribers_CannotSuppressOtherObserversOrCleanup()
    {
        var native = new FakeEngineSession();
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Throws(new InvalidOperationException("Publisher failed"));
        var service = Create(() => native, publisher.Object);
        await using var lifetime = service.ConfigureAwait(false);
        service.StateChanged += (_, _) => throw new InvalidOperationException("Subscriber failed");
        var notification = FaultNotification(service);
        await this.StartAsync(service).ConfigureAwait(false);

        native.Loop.SetResult();
        _ = await notification.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.ShutdownAsync().ConfigureAwait(false);

        _ = service.State.Should().Be(EngineServiceState.NoEngine);
        _ = native.HasContext.Should().BeFalse();
        publisher.Verify(value => value.Publish(It.IsAny<OperationResult>()), Times.Once);
    }

    private static EngineService Create(Func<EngineSession> sessions, IOperationResultPublisher publisher)
        => new(new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! }, sessions, publisher);

    private static Mock<IOperationResultPublisher> Publisher(ConcurrentQueue<OperationResult> results)
    {
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(results.Enqueue);
        return publisher;
    }

    private static Task<EngineStateChangedEventArgs> FaultNotification(IEngineService service)
    {
        var completion = new TaskCompletionSource<EngineStateChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        service.StateChanged += (_, change) =>
        {
            if (change.OperationResult is not null)
            {
                _ = completion.TrySetResult(change);
            }
        };
        return completion.Task;
    }

    private async Task StartAsync(EngineService service)
    {
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);
    }

    private async Task RestartAsync(EngineService service)
    {
        await service.ShutdownAsync().ConfigureAwait(false);
        await this.StartAsync(service).ConfigureAwait(false);
    }
}
