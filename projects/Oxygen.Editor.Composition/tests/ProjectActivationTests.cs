// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Documents;
using DryIoc;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.ProjectExplorer.Activation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     Activating a project replaces the open session only once every window's documents agreed to close, and a
///     workspace navigation that reports a failure closes the session it opened (spec 6.5).
/// </summary>
/// <remarks>
///     The coordinator runs with the editor's own services. The ports it reads the project and the user's answer from
///     are faked: the validation of the project's folder, the recent projects, the router's workspace navigation and,
///     where the user cancels, the close of the documents.
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Project session")]
public sealed class ProjectActivationTests
{
    private const string ActivationFailedCode = DiagnosticCodes.WorkspacePrefix + "ACTIVATION_FAILED";

    private readonly Subject<RouterEvent> routerEvents = new();

    [TestCleanup]
    public void Cleanup() => this.routerEvents.Dispose();

    [TestMethod]
    public async Task ActivateAsync_DocumentsCloseCancelled_KeepsTheOpenSession()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var next = this.Arrange(scopes, navigationFails: false);
        var sessions = scopes.Host.Resolve<ProjectSessionHost>();
        var (open, openProject) = NewProject();
        var kept = await sessions.OpenAsync(open, openProject).ConfigureAwait(false);

        // A window of the session keeps its document when asked.
        _ = scopes.ShowWindows(new OpenWindow(new WindowId(3), DroidNet.Aura.WindowCategory.Document, kept));
        var tabs = sessions.OpenWindow(new WindowId(3), []).Resolver.Resolve<IDocumentTabs>();
        var documentId = DocumentId.New();
        _ = await tabs.OpenDocumentAsync(new WindowId(3), Mock.Of<IDocumentMetadata>(tab => tab.DocumentId == documentId && tab.Title == "Kept")).ConfigureAwait(false);
        tabs.DocumentClosing += (_, args) => args.AddVetoTask(Task.FromResult(false));

        // Act
        var result = await scopes.Host.Resolve<IProjectActivationCoordinator>().ActivateAsync(Open(next)).ConfigureAwait(false);

        // Assert
        _ = result.Status.Should().Be(OperationStatus.Cancelled);
        _ = sessions.Current.Should().BeSameAs(kept);
        _ = kept.Lifetime.IsCancellationRequested.Should().BeFalse();
    }

    [TestMethod]
    public async Task ActivateAsync_NavigationReportsAFailure_ClosesTheSessionItOpened()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var next = this.Arrange(scopes, navigationFails: true);
        var sessions = scopes.Host.Resolve<IProjectSessionHost>();
        IProjectSession? opened = null;
        sessions.SessionChanged += (_, _) => opened ??= sessions.Current;

        // Act
        var result = await scopes.Host.Resolve<IProjectActivationCoordinator>().ActivateAsync(Open(next)).ConfigureAwait(false);

        // Assert
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Select(static diagnostic => diagnostic.Code).Should().Contain(ActivationFailedCode);
        _ = opened!.Context.ProjectId.Should().Be(next.ProjectId);
        _ = opened.Lifetime.IsCancellationRequested.Should().BeTrue();
        _ = sessions.Current.Should().BeNull();
    }

    [TestMethod]
    public async Task ActivateAsync_NavigationSucceeds_ReplacesTheOpenSession()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var next = this.Arrange(scopes, navigationFails: false);
        var sessions = scopes.Host.Resolve<IProjectSessionHost>();
        var (previous, previousProject) = NewProject();
        var replaced = await sessions.OpenAsync(previous, previousProject).ConfigureAwait(false);

        // Act
        var result = await scopes.Host.Resolve<IProjectActivationCoordinator>().ActivateAsync(Open(next)).ConfigureAwait(false);

        // Assert
        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = replaced.Lifetime.IsCancellationRequested.Should().BeTrue();
        _ = sessions.Current!.Context.ProjectId.Should().Be(next.ProjectId);
    }

    private static ProjectActivationRequest Open(ProjectContext project)
        => new()
        {
            Mode = ProjectActivationMode.OpenExisting,
            SourceSurface = ProjectActivationSourceSurface.Open,
            ProjectLocation = project.ProjectRoot.FullPath,
        };

    private static (ProjectContext Context, IProject Project) NewProject()
    {
        var context = TestProject.Create(new MockFileSystem(), $@"C:\ActivationTests\{Guid.NewGuid():N}").Context;
        return (context, new Project(TestProject.InfoOf(context)));
    }

    // The project to activate, validated without reading its folder, and a router whose workspace navigation succeeds
    // or reports a failure.
    private ProjectContext Arrange(EditorScopes scopes, bool navigationFails)
    {
        _ = scopes.ShowWindows();
        var (project, _) = NewProject();
        var validation = new Mock<IProjectValidationService>();
        _ = validation.Setup(service => service.ValidateAsync(project.ProjectRoot.FullPath, It.IsAny<CancellationToken>()))
            .ReturnsAsync(ProjectValidationResult.Valid(project.ProjectRoot.FullPath, TestProject.InfoOf(project)));
        scopes.Host.RegisterInstance(validation.Object, IfAlreadyRegistered.Replace);
        scopes.Host.RegisterInstance(new Mock<IRecentProjectAdapter>().Object, IfAlreadyRegistered.Replace);

        var router = new Mock<IRouter>();
        _ = router.SetupGet(service => service.Events).Returns(this.routerEvents);
        _ = router.Setup(service => service.NavigateAsync(It.IsAny<string>(), It.IsAny<FullNavigation?>()))
            .Callback<string, FullNavigation?>((url, options) => this.routerEvents.OnNext(
                navigationFails ? new NavigationError(options!) : new NavigationEnd(url, options!)))
            .Returns(Task.CompletedTask);
        scopes.Host.RegisterInstance(router.Object, IfAlreadyRegistered.Replace);
        return project;
    }
}
