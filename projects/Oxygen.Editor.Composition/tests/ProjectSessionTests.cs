// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using DryIoc;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.Data;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     The project session as its host opens and closes it: one session at a time, created for one project, whose close
///     cancels its lifetime and disposes its windows and its own services (spec 6.5).
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Project session")]
public sealed class ProjectSessionTests
{
    [TestMethod]
    public async Task OpenAsync_Project_RegistersTheLoadedProjectAndTheSessionInItsScope()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(1), EditorWindows.WorkspaceCategory, Session: null));
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var changes = 0;
        host.SessionChanged += (_, _) => changes++;

        // Act
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);

        // Assert
        _ = host.Current.Should().BeSameAs(session);
        _ = scopes.Host.Resolve<IProjectSessionHost>().Should().BeSameAs(host);
        _ = session.Context.Should().BeSameAs(context);
        var window = host.OpenWindow(new WindowId(1), []);
        _ = window.Resolver.Resolve<IProject>().Should().BeSameAs(project);
        _ = window.Resolver.Resolve<IProjectSession>().Should().BeSameAs(session);
        _ = window.Resolver.Resolve<WindowId>().Should().Be(new WindowId(1));
        scopes.Windows.Verify(manager => manager.SetMetadata(new WindowId(1), EditorWindows.SessionMetadataKey, session), Times.Once());
        _ = scopes.Windows.Object.GetWindow(new WindowId(1))!.DocumentTabSource.Should().BeSameAs(window.Resolver.Resolve<IDocumentTabs>());
        _ = session.Lifetime.IsCancellationRequested.Should().BeFalse();
        _ = changes.Should().Be(1);
    }

    [TestMethod]
    public async Task OpenAsync_WhileASessionIsOpen_Throws()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var open = await host.OpenAsync(context, project).ConfigureAwait(false);
        var (next, nextProject) = NewProject();

        // Act
        var reopen = () => host.OpenAsync(next, nextProject);

        // Assert
        _ = await reopen.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = host.Current.Should().BeSameAs(open);
    }

    [TestMethod]
    public async Task CloseAsync_OpenSession_CancelsItsLifetimeAndDisposesItsWindowsAndServices()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(1), EditorWindows.WorkspaceCategory, Session: null));
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var window = host.OpenWindow(new WindowId(1), []);
        var history = window.Resolver.Resolve<IHistoryService>();
        history.GlobalHistory.AddChange("Edit", new List<int>(), static list => list.Clear());
        var changes = 0;
        host.SessionChanged += (_, _) => changes++;

        // Act
        await host.CloseAsync().ConfigureAwait(false);

        // Assert
        _ = host.Current.Should().BeNull();
        _ = session.Lifetime.IsCancellationRequested.Should().BeTrue();
        _ = window.IsDisposed.Should().BeTrue();
        _ = history.GlobalHistory.CanUndo.Should().BeFalse();
        _ = changes.Should().Be(1);
    }

    [TestMethod]
    public async Task ChangeContext_SameProjectWithNewMounts_ReplacesTheContextAndReportsItOnce()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var next = context with { LocalFolderMounts = [new LocalFolderMount("Shared", FileSystemPath.From(@"D:\Shared"))] };
        var seen = new List<ProjectContext>();
        session.ContextChanged += (_, _) => seen.Add(session.Context);

        // Act
        host.ChangeContext(next);

        // Assert: the session stays open and its services read the new mounts.
        _ = host.Current.Should().BeSameAs(session);
        _ = seen.Should().Equal(next);
        _ = session.Lifetime.IsCancellationRequested.Should().BeFalse();
    }

    [TestMethod]
    public async Task ChangeContext_SameProjectWithNewMounts_KeepsTheSessionsDocumentsAndClipboard()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(1), EditorWindows.WorkspaceCategory, Session: null));
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        _ = await host.OpenAsync(context, project).ConfigureAwait(false);
        var window = host.OpenWindow(new WindowId(1), []);
        var documents = window.Resolver.Resolve<OpenDocuments>();
        var clipboard = window.Resolver.Resolve<ISceneNodeClipboard>();

        // Act
        host.ChangeContext(context with { LocalFolderMounts = [new LocalFolderMount("Shared", FileSystemPath.From(@"D:\Shared"))] });

        // Assert: a mount change keeps every open document and what the clipboard holds.
        _ = window.Resolver.Resolve<OpenDocuments>().Should().BeSameAs(documents);
        _ = window.Resolver.Resolve<ISceneNodeClipboard>().Should().BeSameAs(clipboard);
    }

    [TestMethod]
    public async Task UpdateSettings_ThenAMountChange_KeepsTheNewSettings()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var changes = 0;
        session.SettingsChanged += (_, _) => changes++;
        var updated = session.Settings with { CookOnChange = !session.Settings.CookOnChange };

        // Act
        session.UpdateSettings(updated);
        host.ChangeContext(context with { LocalFolderMounts = [new LocalFolderMount("Shared", FileSystemPath.From(@"D:\Shared"))] });

        // Assert: the settings are the session's, which a mount change does not reset to the context's.
        _ = session.Settings.Should().Be(updated);
        _ = changes.Should().Be(1);
    }

    [TestMethod]
    public async Task PersistentState_ResolvedRepeatedlyInTheEditor_ReusesThePooledContext()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var contexts = scopes.Host.Resolve<Func<PersistentState>>();
        var first = contexts();
        await first.DisposeAsync().ConfigureAwait(false);

        // Act: another read or write, as the Content Explorer's each navigation is.
        var second = contexts();

        // Assert: a disposed context went back to the pool, and the next use rents it again.
        _ = second.Should().BeSameAs(first);
        await second.DisposeAsync().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task PersistentState_InTheSession_IsTheSessionsOneContext()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(1), EditorWindows.WorkspaceCategory, Session: null));
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        _ = await host.OpenAsync(context, project).ConfigureAwait(false);
        var window = host.OpenWindow(new WindowId(1), []);

        // Act
        var first = window.Resolver.Resolve<PersistentState>();
        var second = window.Resolver.Resolve<PersistentState>();

        // Assert: the session's services share one context, which the session returns when it closes.
        _ = second.Should().BeSameAs(first);
    }

    [TestMethod]
    public async Task ChangeContext_AnotherProject_Throws()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var changes = 0;
        session.ContextChanged += (_, _) => changes++;

        // Act
        var otherId = () => host.ChangeContext(context with { ProjectId = Guid.NewGuid() });
        var otherRoot = () => host.ChangeContext(context with { ProjectRoot = FileSystemPath.From(@"C:\Elsewhere") });

        // Assert: another project always opens a new session.
        _ = otherId.Should().Throw<InvalidOperationException>();
        _ = otherRoot.Should().Throw<InvalidOperationException>();
        _ = session.Context.Should().BeSameAs(context);
        _ = changes.Should().Be(0);
    }

    [TestMethod]
    public async Task CloseAsync_WorkspaceWindowOpen_ClosesTheWindowThenDisposesTheSession()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var workspace = new WindowId(7);
        var windows = scopes.ShowWindows(new OpenWindow(workspace, EditorWindows.WorkspaceCategory, session));
        var window = host.OpenWindow(workspace, []);
        var disposedWhenClosed = true;
        _ = windows.Setup(value => value.CloseWindowAsync(workspace))
            .Callback(() => disposedWhenClosed = window.IsDisposed)
            .ReturnsAsync(true);

        // Act
        await host.CloseAsync().ConfigureAwait(false);

        // Assert: the window closes while the session's services still exist, then the session is disposed.
        windows.Verify(value => value.CloseWindowAsync(workspace), Times.Once());
        _ = disposedWhenClosed.Should().BeFalse();
        _ = window.IsDisposed.Should().BeTrue();
        _ = host.Current.Should().BeNull();
    }

    [TestMethod]
    public async Task OpenWindow_ThatAuraDoesNotKnow_Throws()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        _ = await host.OpenAsync(context, project).ConfigureAwait(false);

        // Act
        var open = () => host.OpenWindow(new WindowId(4), []);

        // Assert
        _ = open.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public async Task CloseAsync_WindowThatStaysOpen_LeavesTheSession()
    {
        // Arrange: the window refuses to close.
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        var session = await host.OpenAsync(context, project).ConfigureAwait(false);
        var kept = new WindowId(5);
        var windows = scopes.ShowWindows(new OpenWindow(kept, WindowCategory.Document, session));
        _ = host.OpenWindow(kept, []);
        _ = windows.Setup(value => value.CloseWindowAsync(kept)).ReturnsAsync(false);

        // Act
        await host.CloseAsync().ConfigureAwait(false);

        // Assert: the window shows no tabs of the closed session and no longer carries it.
        _ = windows.Object.GetWindow(kept)!.DocumentTabSource.Should().BeNull();
        windows.Verify(value => value.RemoveMetadata(kept, EditorWindows.SessionMetadataKey), Times.Once());
    }

    [TestMethod]
    public async Task CloseAsync_NoSession_DoesNothing()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows();
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var changes = 0;
        host.SessionChanged += (_, _) => changes++;

        // Act
        await host.CloseAsync().ConfigureAwait(false);

        // Assert
        _ = host.Current.Should().BeNull();
        _ = changes.Should().Be(0);
    }

    [TestMethod]
    public async Task OpenAsync_AfterClose_CreatesTheNextSessionsOwnServices()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(1), EditorWindows.WorkspaceCategory, Session: null));
        var host = scopes.Host.Resolve<ProjectSessionHost>();
        var (context, project) = NewProject();
        _ = await host.OpenAsync(context, project).ConfigureAwait(false);
        var before = host.OpenWindow(new WindowId(1), []).Resolver.Resolve<IHistoryService>();
        await host.CloseAsync().ConfigureAwait(false);
        var (next, nextProject) = NewProject();

        // Act
        _ = await host.OpenAsync(next, nextProject).ConfigureAwait(false);

        // Assert
        _ = host.Current!.Context.Should().BeSameAs(next);
        _ = host.OpenWindow(new WindowId(1), []).Resolver.Resolve<IHistoryService>().Should().NotBeSameAs(before);
    }

    private static (ProjectContext Context, IProject Project) NewProject()
    {
        var context = TestProject.Create(new MockFileSystem()).Context;
        return (context, new Project(TestProject.InfoOf(context)));
    }
}
