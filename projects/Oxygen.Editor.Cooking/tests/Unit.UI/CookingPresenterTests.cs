// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Windows;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Cooking.Unit.UI.Tests;

/// <summary>The Cooking panel comes forward when a cook run asks to be revealed, and only while its presenter lives.</summary>
[TestClass]
internal sealed class CookingPresenterTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>A revealed run brings the panel forward once.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task RevealedRunBringsThePanelForward() => EnqueueAsync(async () =>
    {
        var placement = new Mock<IPlacementService>();
        var runs = new Mock<ICookRunService>();
        using var session = CreateSession();
        var (model, snapshot) = CreateModel(session, runs);
        using (model)
        using (new CookingPresenter(model, placement.Object))
        {
            await RaiseRevealAsync(runs, snapshot).ConfigureAwait(true);
        }

        placement.Verify(service => service.ShowPanelAsync("cook", It.IsAny<WindowPlacement>()), Times.Once());
    });

    /// <summary>A disposed presenter no longer brings the panel forward.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public Task DisposedPresenterDoesNotBringThePanelForward() => EnqueueAsync(async () =>
    {
        var placement = new Mock<IPlacementService>();
        var runs = new Mock<ICookRunService>();
        using var session = CreateSession();
        var (model, snapshot) = CreateModel(session, runs);
        using (model)
        {
            var presenter = new CookingPresenter(model, placement.Object);
            presenter.Dispose();
            await RaiseRevealAsync(runs, snapshot).ConfigureAwait(true);
        }

        placement.Verify(service => service.ShowPanelAsync("cook", It.IsAny<WindowPlacement>()), Times.Never());
    });

    // The model applies a run change on the dispatcher at low priority; a later item of the same priority runs after it.
    private static async Task RaiseRevealAsync(Mock<ICookRunService> runs, CookRunSnapshot snapshot)
    {
        RaiseReveal(runs, snapshot);
        var applied = new TaskCompletionSource();
        _ = DispatcherQueue.GetForCurrentThread().TryEnqueue(DispatcherQueuePriority.Low, applied.SetResult);
        await applied.Task.ConfigureAwait(true);
    }

    // Run changes are a synchronous event.
    private static void RaiseReveal(Mock<ICookRunService> runs, CookRunSnapshot snapshot)
        => runs.Raise(service => service.RunChanged += null, runs.Object, new CookRunChangedEventArgs(snapshot, reveal: true));

    private static TestSession CreateSession()
        => new(TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))).Context with { Name = "Presenter" });

    private static (CookingPanelViewModel Model, CookRunSnapshot Snapshot) CreateModel(TestSession session, Mock<ICookRunService> runs)
    {
        var project = session.Context;
        var snapshot = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(),
            ProjectId = project.ProjectId,
            ProjectRoot = project.ProjectRoot,
            DisplayName = "Project",
            Request = new(CookTargetKind.Project, ScopeUri: null),
        };
        _ = runs.SetupGet(service => service.Runs).Returns([]);
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) };
        return (new CookingPanelViewModel(runs.Object, Mock.Of<IContentPipelineService>(), session, Mock.Of<ICookRecovery>(), hosting), snapshot);
    }
}
