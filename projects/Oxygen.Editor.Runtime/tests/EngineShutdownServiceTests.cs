// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class EngineShutdownServiceTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task HostedServiceResolution_DoesNotRequireTheUiDispatcher()
    {
        var container = HostContainer.Create();
        await using var lifetime = container.ConfigureAwait(false);
        var hosting = new HostingContext { Application = null!, Dispatcher = null!, DispatcherScheduler = null! };
        container.RegisterInstance(hosting);
        container.RegisterInstance(Mock.Of<IEngineService>());
        container.RegisterInstance(Mock.Of<IOperationResultPublisher>());
        container.RegisterInstance(Mock.Of<IHostedService>());
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
        using var container = HostContainer.Create();
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
    public async Task Shutdown_RunningEngine_ShutsItDownOnTheUiThreadOnce()
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        var dispatched = 0;
        var service = new EngineShutdownService(engine.Object, Mock.Of<IOperationResultPublisher>(), action =>
        {
            dispatched++;
            return action();
        });

        await service.ShutdownAsync().ConfigureAwait(false);

        _ = dispatched.Should().Be(1);
        engine.Verify(value => value.ShutdownAsync(), Times.Once());
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
