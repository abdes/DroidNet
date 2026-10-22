// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Composition;
using DroidNet.Docking;
using DryIoc;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     Switching projects closes the project's session, with its workspace window, and opens another session and window.
///     The session's global history and the preferences stored per project are the next project's own, and the session's
///     state database contexts end with it; the gizmo preferences, kept across projects, are the editor's (scope-topology.md 5.3).
///     The previous and the next workspace windows, open together during the switch, each have their own panels.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Project switch")]
public sealed class ProjectSwitchTests
{
    private static readonly Type[] ProjectPreferences = [typeof(PreviewSettingsService), typeof(SceneNodeInteraction), typeof(ViewportStateService)];

    [TestMethod]
    public async Task SwitchProject_ProjectPreferences_AreTheNextSessionsOwn()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var firstSession = scopes.CreateSession();
        var first = scopes.CreateWindow(firstSession);
        var before = ProjectPreferences.Select(type => first.Resolver.Resolve(type)).ToList();
        var settingsBefore = first.Resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService));

        // Act
        await firstSession.DisposeAsync().ConfigureAwait(false);
        var nextSession = scopes.CreateSession();
        var next = scopes.CreateWindow(nextSession);

        // Assert
        _ = ProjectPreferences.Select(type => next.Resolver.Resolve(type)).Should().NotIntersectWith(before)
            .And.Equal(ProjectPreferences.Select(type => nextSession.Resolver.Resolve(type)));
        _ = next.Resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService)).Should().NotBeSameAs(settingsBefore);
    }

    [TestMethod]
    public async Task CloseSession_StateDatabaseContextOfThePreviewSettings_IsDisposedWithTheSession()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var context = session.Resolver.Resolve<PersistentState>();

        // Act
        await session.DisposeAsync().ConfigureAwait(false);

        // Assert: the editor scope keeps no context of a closed session.
        var use = () => context.ChangeTracker;
        _ = use.Should().Throw<ObjectDisposedException>();
    }

    [TestMethod]
    public async Task SwitchProject_GlobalHistory_IsTheNextSessionsOwn()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var firstSession = scopes.CreateSession();
        var before = scopes.CreateWindow(firstSession).Resolver.Resolve<IHistoryService>();

        // Act
        await firstSession.DisposeAsync().ConfigureAwait(false);
        var nextSession = scopes.CreateSession();
        var next = scopes.CreateWindow(nextSession).Resolver.Resolve<IHistoryService>();

        // Assert
        _ = next.Should().NotBeSameAs(before).And.BeSameAs(nextSession.Resolver.Resolve<IHistoryService>());
    }

    [TestMethod]
    public async Task SwitchProject_SceneClipboard_IsTheNextSessionsOwn()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var firstSession = scopes.CreateSession();
        var before = scopes.CreateWindow(firstSession).Resolver.Resolve<ISceneNodeClipboard>();

        // Act
        await firstSession.DisposeAsync().ConfigureAwait(false);
        var nextSession = scopes.CreateSession();
        var next = scopes.CreateWindow(nextSession).Resolver.Resolve<ISceneNodeClipboard>();

        // Assert: nothing staged in one project is pasted into another.
        _ = next.Should().NotBeSameAs(before).And.BeSameAs(nextSession.Resolver.Resolve<ISceneNodeClipboard>());
    }

    [TestMethod]
    public async Task SwitchProject_WhileThePreviousWorkspaceIsOpen_EachWindowHasItsOwnPanels()
    {
        // Arrange: the router activates the next workspace before the previous window closes, and both use the same
        // panel ids.
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var previous = scopes.CreateWindow(scopes.CreateSession());
        var next = scopes.CreateWindow(scopes.CreateSession());
        var previousLog = DockMinimizedPanel(previous, "log");
        var nextLog = DockMinimizedPanel(next, "log");

        // Act
        var shown = await next.Resolver.Resolve<IPlacementService>().ShowPanelAsync("log").ConfigureAwait(false);

        // Assert
        _ = shown.Should().BeTrue();
        _ = next.Resolver.Resolve<IDocker>().FindDockable("log").Should().BeSameAs(nextLog);
        _ = previous.Resolver.Resolve<IDocker>().FindDockable("log").Should().BeSameAs(previousLog);
        _ = nextLog.Owner!.State.Should().Be(DockingState.Pinned);
        _ = previousLog.Owner!.State.Should().Be(DockingState.Minimized);
    }

    [TestMethod]
    public async Task SwitchProject_GizmoPreferences_AreTheEditors()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var firstSession = scopes.CreateSession();
        var first = scopes.CreateWindow(firstSession);
        var before = first.Resolver.Resolve<TransformToolSettingsService>();

        // Act
        await firstSession.DisposeAsync().ConfigureAwait(false);
        var next = scopes.CreateWindow(scopes.CreateSession());

        // Assert
        _ = next.Resolver.Resolve<TransformToolSettingsService>().Should().BeSameAs(before)
            .And.BeSameAs(scopes.Host.Resolve<TransformToolSettingsService>());
    }

    [TestMethod]
    public async Task Registrations_ProjectPreferences_AreTheSessionsNotTheEditors()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);

        // Act
        var session = scopes.CreateSession();

        // Assert: removed from the editor scope, so the session's registrations are not shadows.
        _ = ProjectPreferences.Where(type => ScopeRegistrations.CanProvide(scopes.Root, type, serviceKey: null)).Should().BeEmpty();
        _ = ScopeRegistrations.CanProvide(scopes.Root, typeof(IEditorSettingsManager), nameof(PreviewSettingsService)).Should().BeFalse();
        _ = ProjectPreferences.Where(type => !ScopeTopology.Owned(session).Any(registration => registration.ServiceType == type)).Should().BeEmpty();
    }

    private static Dockable DockMinimizedPanel(ScopeHandle window, string panelId)
    {
        var docker = window.Resolver.Resolve<IDocker>();
        var dock = docker.CreateDock<ToolDock>();
        var panel = docker.CreateDockable<Dockable>(panelId);
        dock.AdoptDockable(panel);
        var anchor = new AnchorBottom();
        try
        {
            docker.Dock(dock, anchor, minimized: true);
            anchor = null;
        }
        finally
        {
            anchor?.Dispose();
        }

        return panel;
    }
}
