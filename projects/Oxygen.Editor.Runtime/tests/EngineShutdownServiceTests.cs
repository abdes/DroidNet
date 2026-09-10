// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class EngineShutdownServiceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task HostedServiceResolution_DoesNotRequireTheUiDispatcherOrWindowManager()
    {
        var container = new Container();
        await using var lifetime = container.ConfigureAwait(false);
        var hosting = new HostingContext { Application = null!, Dispatcher = null!, DispatcherScheduler = null! };
        container.RegisterInstance(hosting);
        container.RegisterInstance(Mock.Of<IEngineService>());
        container.RegisterInstance(Mock.Of<IOperationResultPublisher>());
        container.RegisterInstance(Mock.Of<IHostedService>());
        container.RegisterDelegate<IWindowManagerService>(_ => throw new InvalidOperationException("UI dispatcher has not started"), Reuse.Singleton);
        container.Register<EngineShutdownService>(Reuse.Singleton);
        container.RegisterDelegate<IHostedService>(resolver => resolver.Resolve<EngineShutdownService>(), Reuse.Singleton);

        var hostedServices = container.ResolveMany<IHostedService>().ToArray();
        _ = hostedServices.Should().HaveCount(2);
        var shutdown = hostedServices.OfType<EngineShutdownService>().Single();
        _ = shutdown.Should().BeSameAs(container.Resolve<EngineShutdownService>());
        await shutdown.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = hosting.Dispatcher.Should().BeNull();
        await shutdown.StopAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    [TestMethod]
    public void SynchronousContainerDisposal_DoesNotInvokeAsyncOnlyCleanup()
    {
        var engine = new Mock<IEngineService>();
        using var container = new Container();
        container.RegisterDelegate<IEngineService>(_ => engine.Object, Reuse.Singleton);
        _ = container.Resolve<IEngineService>();

        container.Dispose();

        engine.Verify(value => value.DisposeAsync(), Times.Never());
    }

    [TestMethod]
    public async Task Host_StopsRuntimeBeforeItsUiService()
    {
        var uiRunning = false;
        var ui = new Mock<IHostedService>();
        _ = ui.Setup(value => value.StartAsync(It.IsAny<CancellationToken>())).Callback(() => uiRunning = true).Returns(Task.CompletedTask);
        _ = ui.Setup(value => value.StopAsync(It.IsAny<CancellationToken>())).Callback(() => uiRunning = false).Returns(Task.CompletedTask);
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.Setup(value => value.ShutdownAsync()).Callback(() => _ = uiRunning.Should().BeTrue()).Returns(ValueTask.CompletedTask);
        var runtime = new EngineShutdownService(engine.Object, Mock.Of<IOperationResultPublisher>(), action => action());
        using var host = new HostBuilder().ConfigureServices(services =>
        {
            _ = services.AddSingleton(ui.Object);
            _ = services.AddSingleton<IHostedService>(runtime);
        }).Build();

        await host.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await host.StopAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        engine.Verify(value => value.ShutdownAsync(), Times.Once());
        _ = uiRunning.Should().BeFalse();
    }

    [TestMethod]
    public async Task OverlappingFinalWindows_ShutDownBeforeBothDisappear()
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.OpenWindows).Returns([Mock.Of<IManagedWindow>(window => window.Id == new WindowId(1)), Mock.Of<IManagedWindow>(window => window.Id == new WindowId(2))]);
        var service = new EngineShutdownService(engine.Object, Mock.Of<IOperationResultPublisher>(), action => action());
        service.ObserveWindows(windows.Object);
        await service.StartAsync(CancellationToken.None).ConfigureAwait(false);
        foreach (var id in (ulong[])[1, 2])
        {
            var args = new WindowClosingEventArgs { WindowId = new WindowId(id) };
            await windows.RaiseAsync(value => value.WindowClosing += null, windows.Object, args).ConfigureAwait(false);
            await args.CompleteAsync(approved: true).ConfigureAwait(false);
        }

        engine.Verify(value => value.ShutdownAsync(), Times.Once());
    }

    [TestMethod]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(false, false)]
    public async Task FinalWindow_ShutsDownOnlyAfterApprovedDocumentCommit(bool guardCanceled, bool commitCanceled)
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.OpenWindows).Returns([Mock.Of<IManagedWindow>(window => window.Id == new WindowId(1))]);
        var committed = false;
        _ = engine.Setup(value => value.ShutdownAsync()).Callback(() => _ = committed.Should().BeTrue()).Returns(ValueTask.CompletedTask);
        var service = new EngineShutdownService(engine.Object, Mock.Of<IOperationResultPublisher>(), action => action());
        service.ObserveWindows(windows.Object);
        await service.StartAsync(CancellationToken.None).ConfigureAwait(false);
        var args = new WindowClosingEventArgs { WindowId = new WindowId(1), Cancel = guardCanceled };
        await windows.RaiseAsync(value => value.WindowClosing += null, windows.Object, args).ConfigureAwait(false);
        args.AddCompletionTask(approved =>
        {
            args.Cancel |= commitCanceled;
            committed = approved && !args.Cancel;
            return Task.CompletedTask;
        });

        await args.CompleteAsync(approved: !guardCanceled).ConfigureAwait(false);
        engine.Verify(value => value.ShutdownAsync(), !guardCanceled && !commitCanceled ? Times.Once() : Times.Never());
    }

    [TestMethod]
    public async Task ClosingOneOfSeveralWindows_KeepsSharedEngineAlive()
    {
        var engine = new Mock<IEngineService>();
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.OpenWindows).Returns([Mock.Of<IManagedWindow>(window => window.Id == new WindowId(1)), Mock.Of<IManagedWindow>(window => window.Id == new WindowId(2))]);
        var service = new EngineShutdownService(engine.Object, Mock.Of<IOperationResultPublisher>(), action => action());
        service.ObserveWindows(windows.Object);
        await service.StartAsync(CancellationToken.None).ConfigureAwait(false);
        var args = new WindowClosingEventArgs { WindowId = new WindowId(1) };
        await windows.RaiseAsync(value => value.WindowClosing += null, windows.Object, args).ConfigureAwait(false);
        await args.CompleteAsync(approved: true).ConfigureAwait(false);
        engine.Verify(value => value.ShutdownAsync(), Times.Never());
    }

    [TestMethod]
    public async Task HostShutdown_ObservesFailureAndPublishesItWithoutThrowing()
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Faulted);
        _ = engine.Setup(value => value.ShutdownAsync()).ThrowsAsync(new AggregateException("Native stop failed"));
        var results = new Mock<IOperationResultPublisher>();
        var dispatched = false;
        var service = new EngineShutdownService(engine.Object, results.Object, action =>
        {
            dispatched = true;
            return action();
        });

        await service.StopAsync(CancellationToken.None).ConfigureAwait(false);
        _ = dispatched.Should().BeTrue();
        results.Verify(value => value.Publish(It.Is<OperationResult>(result => string.Equals(result.OperationKind, "Runtime.Shutdown", StringComparison.Ordinal) && result.Status == OperationStatus.Failed)), Times.Once());
    }

    [TestMethod]
    public async Task HostShutdown_DoesNotRequireDispatcherAfterEngineWasReleased()
    {
        var service = new EngineShutdownService(Mock.Of<IEngineService>(), Mock.Of<IOperationResultPublisher>(), _ => throw new InvalidOperationException("Dispatcher stopped"));
        await service.StopAsync(CancellationToken.None).ConfigureAwait(false);
    }
}
