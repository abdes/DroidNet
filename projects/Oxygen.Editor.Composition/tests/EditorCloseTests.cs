// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Tests;
using DryIoc;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Services;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     The editor's own parts of closing a window (shell design section 6): closing a project session's last workspace
///     window opens the Start window before the workspace window closes, then closes the session; closing the editor's
///     last window otherwise exits it, closing any session before the engine shuts down.
/// </summary>
/// <remarks>
///     The participants run with the editor's services on the UI thread. The router, which would open real windows, and
///     the engine are faked.
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Project session")]
public sealed class EditorCloseTests : VisualUserInterfaceTests
{
    private static readonly WindowId Workspace = new(7);
    private static readonly WindowId Start = new(9);

    [TestMethod]
    public Task Close_SessionsLastWindow_OpensTheStartWindowThenClosesTheSession() => EnqueueAsync(async () =>
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        var sessions = scopes.Host.Resolve<ProjectSessionHost>();
        var sessionOpenWhenStartOpened = false;
        var router = Router(scopes, events, navigationFails: false, () => sessionOpenWhenStartOpened = sessions.Current is not null);
        var engine = Engine(scopes);
        await OpenSessionAsync(sessions).ConfigureAwait(true);
        _ = scopes.ShowWindows(new OpenWindow(Workspace, EditorWindows.WorkspaceCategory, sessions.Current));

        // Act
        var closed = await CloseAsync(scopes, new WindowClose { Window = Workspace, IsLastWindow = true }).ConfigureAwait(true);

        // Assert: the editor is never without a window, and the session closes after the Start window opened.
        _ = closed.Should().BeTrue();
        _ = sessionOpenWhenStartOpened.Should().BeTrue();
        router.Verify(
            value => value.NavigateAsync(EditorWindows.StartPage, It.Is<FullNavigation?>(navigation => navigation!.Target!.Name == EditorWindows.StartTarget)),
            Times.Once());
        _ = sessions.Current.Should().BeNull();
        engine.Verify(value => value.ShutdownAsync(), Times.Never());
    });

    [TestMethod]
    public Task Close_StartWindowDoesNotOpen_KeepsTheSessionsLastWindowOpen() => EnqueueAsync(async () =>
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        _ = Router(scopes, events, navigationFails: true);
        _ = Engine(scopes);
        var results = new Mock<IOperationResultPublisher>();
        scopes.Host.RegisterInstance(results.Object, IfAlreadyRegistered.Replace);
        var sessions = scopes.Host.Resolve<ProjectSessionHost>();
        await OpenSessionAsync(sessions).ConfigureAwait(true);
        var session = sessions.Current;
        _ = scopes.ShowWindows(new OpenWindow(Workspace, EditorWindows.WorkspaceCategory, session));

        // Act
        var closed = await CloseAsync(scopes, new WindowClose { Window = Workspace, IsLastWindow = true }).ConfigureAwait(true);

        // Assert: the user learns why the workspace stayed open.
        _ = closed.Should().BeFalse();
        _ = sessions.Current.Should().BeSameAs(session);
        results.Verify(
            value => value.Publish(It.Is<OperationResult>(result => result.Status == OperationStatus.Failed
                && result.Diagnostics.Any(diagnostic => diagnostic.Code == DiagnosticCodes.WorkspacePrefix + "START_WINDOW_FAILED"))),
            Times.Once());
    });

    [TestMethod]
    public Task Close_LastWindowOutsideTheSession_ExitsTheEditor() => EnqueueAsync(async () =>
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        var router = Router(scopes, events, navigationFails: false);
        var engine = Engine(scopes);
        _ = scopes.ShowWindows(new OpenWindow(Start, EditorWindows.StartCategory, Session: null));

        // Act
        var closed = await CloseAsync(scopes, new WindowClose { Window = Start, IsLastWindow = true }).ConfigureAwait(true);

        // Assert
        _ = closed.Should().BeTrue();
        engine.Verify(value => value.ShutdownAsync(), Times.Once());
        router.Verify(value => value.NavigateAsync(It.IsAny<string>(), It.IsAny<FullNavigation?>()), Times.Never());
    });

    [TestMethod]
    public Task Close_WindowWithOthersOpen_LeavesTheSessionAndTheEngine() => EnqueueAsync(async () =>
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        var router = Router(scopes, events, navigationFails: false);
        var engine = Engine(scopes);
        var sessions = scopes.Host.Resolve<ProjectSessionHost>();
        await OpenSessionAsync(sessions).ConfigureAwait(true);
        _ = scopes.ShowWindows(new OpenWindow(Workspace, EditorWindows.WorkspaceCategory, sessions.Current), new OpenWindow(Start, EditorWindows.StartCategory, Session: null));

        // Act: the Start window closes while the workspace window stays open.
        var closed = await CloseAsync(scopes, new WindowClose { Window = Start, IsLastWindow = false }).ConfigureAwait(true);

        // Assert
        _ = closed.Should().BeTrue();
        _ = sessions.Current.Should().NotBeNull();
        engine.Verify(value => value.ShutdownAsync(), Times.Never());
        router.Verify(value => value.NavigateAsync(It.IsAny<string>(), It.IsAny<FullNavigation?>()), Times.Never());
    });

    [TestMethod]
    public Task Close_SessionsLastWorkspaceWindowWhileAnotherSessionWindowKeepsItsDocuments_KeepsTheSessionOpen() => EnqueueAsync(async () =>
    {
        // Arrange: a document window of the session keeps its documents when asked.
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        var router = Router(scopes, events, navigationFails: false);
        _ = Engine(scopes);
        var sessions = scopes.Host.Resolve<ProjectSessionHost>();
        await OpenSessionAsync(sessions).ConfigureAwait(true);
        var document = new WindowId(8);
        _ = scopes.ShowWindows(new OpenWindow(Workspace, EditorWindows.WorkspaceCategory, sessions.Current), new OpenWindow(document, WindowCategory.Document, sessions.Current));
        await KeepADocumentAsync(sessions, document).ConfigureAwait(true);

        // Act
        var prepared = await PrepareAsync(scopes, new WindowClose { Window = Workspace, IsLastWindow = false }).ConfigureAwait(true);

        // Assert: the session's windows close together or not at all.
        _ = prepared.Should().BeFalse();
        _ = sessions.Current.Should().NotBeNull();
        router.Verify(value => value.NavigateAsync(It.IsAny<string>(), It.IsAny<FullNavigation?>()), Times.Never());
    });

    [TestMethod]
    public Task Close_FinalizedOffTheUiThread_SavesThePlacementReadOnTheUiThread() => EnqueueAsync(async () =>
    {
        // Arrange: a window with a saved placement, restored when it opened.
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(true);
        using var events = new Subject<RouterEvent>();
        var router = Router(scopes, events, navigationFails: false);
        await using var database = new SqliteConnection("Data Source=:memory:");
        await database.OpenAsync().ConfigureAwait(true);
        PersistentState State() => new(new DbContextOptionsBuilder<PersistentState>().UseSqlite(database).Options);
        await using (var seed = State())
        {
            _ = await seed.Database.EnsureCreatedAsync().ConfigureAwait(true);
            _ = seed.Set<WindowPlacements>().Add(new WindowPlacements { PlacementKey = "workspace", PlacementData = "opened" });
            _ = await seed.SaveChangesAsync().ConfigureAwait(true);
        }

        using var lifecycle = new Subject<WindowLifecycleEvent>();
        var restored = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        bool? readOnUiThread = null;
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.WindowEvents).Returns(lifecycle);
        _ = windows.Setup(value => value.RestoreWindowPlacementAsync(Workspace, "opened")).Callback(restored.SetResult).Returns(Task.CompletedTask);
        _ = windows.Setup(value => value.GetWindowPlacementString(Workspace)).Returns(() =>
        {
            readOnUiThread = DispatcherQueue.GetForCurrentThread() is not null;
            return "closed";
        });
        using var placements = new WindowPlacementService(windows.Object, router.Object, State, scopes.Host.Resolve<HostingContext>());
        var window = new Mock<IManagedWindow>();
        _ = window.SetupGet(value => value.Id).Returns(Workspace);
        _ = window.SetupGet(value => value.Metadata).Returns(new Dictionary<string, object>(StringComparer.Ordinal) { ["WindowPlacementKey"] = "workspace" });
        lifecycle.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Created, window.Object));
        await restored.Task.ConfigureAwait(true);
        var step = await placements.PrepareAsync(new WindowClose { Window = Workspace, IsLastWindow = true }).ConfigureAwait(true);

        // Act: Aura may finalize a close on a thread-pool thread.
        await Task.Run(() => step!.FinalizeAsync()).ConfigureAwait(true);

        // Assert: the window is read on the UI thread, which owns it, and its placement is saved.
        _ = readOnUiThread.Should().BeTrue();
        await using var saved = State();
        _ = (await saved.Set<WindowPlacements>().SingleAsync(record => record.PlacementKey == "workspace").ConfigureAwait(true))
            .PlacementData.Should().Be("closed");
    });

    // A router whose navigations succeed or report a failure, on the UI thread the participants dispatch to.
    private static Mock<IRouter> Router(EditorScopes scopes, Subject<RouterEvent> events, bool navigationFails, Action? navigated = null)
    {
        var hosting = new HostingContext
        {
            Dispatcher = DispatcherQueue.GetForCurrentThread(),
            Application = Application.Current,
            DispatcherScheduler = new DispatcherQueueScheduler(DispatcherQueue.GetForCurrentThread()),
        };
        scopes.Host.RegisterInstance(hosting, IfAlreadyRegistered.Replace);

        var router = new Mock<IRouter>();
        _ = router.SetupGet(service => service.Events).Returns(events);
        _ = router.Setup(service => service.NavigateAsync(It.IsAny<string>(), It.IsAny<FullNavigation?>()))
            .Callback<string, FullNavigation?>((url, options) =>
            {
                navigated?.Invoke();
                events.OnNext(navigationFails ? new NavigationError(options!) : new NavigationEnd(url, options!));
            })
            .Returns(Task.CompletedTask);
        scopes.Host.RegisterInstance(router.Object, IfAlreadyRegistered.Replace);
        return router;
    }

    private static Mock<IEngineService> Engine(EditorScopes scopes)
    {
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(Mock.Of<IRuntimeWorldCommands>());
        _ = engine.Setup(value => value.ShutdownAsync()).Returns(ValueTask.CompletedTask);
        scopes.Host.RegisterInstance(engine.Object, IfAlreadyRegistered.Replace);
        return engine;
    }

    private static async Task OpenSessionAsync(ProjectSessionHost sessions)
    {
        var context = TestProject.Create(new MockFileSystem()).Context;
        _ = await sessions.OpenAsync(context, new Project(TestProject.InfoOf(context))).ConfigureAwait(true);
    }

    // A window of the session with a document whose close is vetoed.
    private static async Task KeepADocumentAsync(ProjectSessionHost sessions, WindowId window)
    {
        var tabs = sessions.OpenWindow(window, []).Resolver.Resolve<IDocumentTabs>();
        var documentId = DocumentId.New();
        _ = await tabs.OpenDocumentAsync(window, Mock.Of<IDocumentMetadata>(tab => tab.DocumentId == documentId && tab.Title == "Kept")).ConfigureAwait(true);
        tabs.DocumentClosing += (_, args) => args.AddVetoTask(Task.FromResult(false));
    }

    // Prepares a close with the editor's own participants; false when one keeps the window open.
    private static async Task<bool> PrepareAsync(EditorScopes scopes, WindowClose close)
        => await PrepareStepsAsync(scopes, close).ConfigureAwait(true) is not null;

    private static async Task<List<WindowCloseStep>?> PrepareStepsAsync(EditorScopes scopes, WindowClose close)
    {
        var participants = scopes.Host.Resolve<IEnumerable<IWindowCloseParticipant>>()
            .Where(static participant => participant.Level is WindowCloseLevel.Session or WindowCloseLevel.Application)
            .OrderBy(static participant => participant.Level)
            .ToList();
        var steps = new List<WindowCloseStep>();
        foreach (var participant in participants)
        {
            if (await participant.PrepareAsync(close).ConfigureAwait(true) is not { } step)
            {
                return null;
            }

            steps.Add(step);
        }

        return steps;
    }

    // Runs the editor's participants through a close as the close and exit service does: every one prepares, then every
    // step commits, then every step finalizes.
    private static async Task<bool> CloseAsync(EditorScopes scopes, WindowClose close)
    {
        if (await PrepareStepsAsync(scopes, close).ConfigureAwait(true) is not { } steps)
        {
            return false;
        }

        foreach (var step in steps)
        {
            if (!await step.CommitAsync().ConfigureAwait(true))
            {
                return false;
            }
        }

        foreach (var step in steps)
        {
            await step.FinalizeAsync().ConfigureAwait(true);
        }

        return true;
    }

}
