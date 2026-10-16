// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using DryIoc;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies scene conflict actions through the real document view model.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class SceneEditorConflictTests
{
    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A reload after a save conflict installs the scene on disk, ends the conflict and tells the views.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ConflictReloadInstallsTheSceneOnDisk()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        var reloaded = new List<Scene>();
        fixture.Messenger.Register<SceneReloadedMessage>(this, (_, message) =>
        {
            reloaded.Add(message.Scene);
            message.Reply(Task.FromResult(true));
        });
        await fixture.Editor.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();

        var reload = await fixture.Editor.ReloadFromDiskAsync().ConfigureAwait(false);

        _ = reload.Succeeded.Should().BeTrue(reload.Message);
        _ = fixture.Editor.HasSaveConflict.Should().BeFalse();
        var current = fixture.Editor.Metadata.Scene;
        _ = current.Should().NotBeSameAs(fixture.Scene);
        _ = current!.RootNodes.Should().ContainSingle().Which.Name.Should().Be("Outside");
        _ = reloaded.Should().Equal(current);
        _ = fixture.Editor.Document.IsDirty.Should().BeFalse();
        fixture.Prompt.Verify(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>()), Times.Never);
        fixture.Editor.ResumeEditing();
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
    }

    /// <summary>A reload whose scene the Scene Explorer cannot show warns that the tab must be reopened.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadTheTreeCannotShowWarns()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        fixture.Messenger.Register<SceneReloadedMessage>(this, (_, message) => message.Reply(Task.FromResult(false)));
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);

        _ = (await fixture.Editor.ReloadFromDiskAsync().ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = fixture.Results.Published.Should().ContainSingle(result => result.OperationKind == SceneOperationKinds.Reload)
            .Which.Severity.Should().Be(DiagnosticSeverity.Warning);
    }

    /// <summary>Saving a copy writes a separate scene and leaves the original dirty and in conflict.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SceneCopyRetainsOriginalDirtyStateAndConflict()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);

        var copy = await fixture.Editor.SaveCopyAsync("Copy").ConfigureAwait(false);

        _ = copy.Succeeded.Should().BeTrue(copy.Message);
        _ = fixture.Files.Read("Copy").Name.Should().Be("Copy");
        _ = fixture.Editor.Document.IsDirty.Should().BeTrue();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();
        _ = fixture.Editor.Document.File.Path.Should().Be(fixture.Files.PathOf("Main"));
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();
    }

    /// <summary>A close request waits for already-submitted reload work before retiring the document.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CloseWaitsForPendingSceneRecovery()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: false).ConfigureAwait(false);
        var sessions = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Commands.Setup(value => value.CompleteEditSessionsAsync(It.IsAny<Scene>(), false)).Returns(sessions.Task);
        var reload = fixture.Editor.ReloadFromDiskAsync();
        var close = fixture.Editor.CloseAsync(discard: true);

        _ = close.IsCompleted.Should().BeFalse();
        fixture.Sync.Verify(value => value.CloseDocument(fixture.Editor.Document.Id), Times.Never);
        sessions.SetResult();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await close.ConfigureAwait(false);
        fixture.Sync.Verify(value => value.CloseDocument(fixture.Editor.Document.Id), Times.Once);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly Container container = new();

        private Fixture(Mock<ISceneDocumentCommandService> commands, SceneDocumentFiles files, Document document)
        {
            this.Commands = commands;
            this.Files = files;
            this.Scene = ((SceneContent)document.Content).Scene;
            this.Metadata = SceneDocumentMetadata.For(document);
        }

        public SceneDocumentFiles Files { get; }

        public Scene Scene { get; }

        public SceneDocumentMetadata Metadata { get; }

        public SceneEditorViewModel Editor { get; private set; } = null!;

        public StrongReferenceMessenger Messenger { get; } = new();

        public Mock<ISceneEngineSync> Sync { get; } = new();

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public Mock<IDocumentConflictPrompt> Prompt { get; } = new();

        public CapturingResults Results { get; } = new();

        /// <summary>Opens "Main" with an unsaved edit after another program replaced its file, in a scene editor.</summary>
        /// <param name="test">The running test.</param>
        /// <param name="active">Whether the scene's tab is the active document.</param>
        /// <param name="previewSettings">The preview preferences, or a default service.</param>
        /// <param name="commitInput">Runs when the editor commits the focused control's input.</param>
        /// <param name="synchronized">Whether the scene is already in the engine.</param>
        /// <returns>The fixture.</returns>
        public static async Task<Fixture> CreateAsync(TestContext test, bool active, Oxygen.Editor.World.Workspace.PreviewSettingsService? previewSettings = null, Action? commitInput = null, bool synchronized = false)
        {
            var commands = new Mock<ISceneDocumentCommandService>();
            _ = commands.Setup(value => value.CompleteEditSessionsAsync(It.IsAny<Scene>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
            var files = new SceneDocumentFiles(commands.Object);
            var id = Guid.NewGuid();
            files.Write("Main", SceneDocumentFiles.Data(id, "Main"));
            var (document, _) = await files.OpenAsync("Main", test.CancellationToken).ConfigureAwait(false);
            document.History.AddChange("Edit", static () => { });
            files.Write("Main", SceneDocumentFiles.Data(id, "Main", "Outside"));
            var fixture = new Fixture(commands, files, document);
            fixture.CreateEditor(active, previewSettings, commitInput, synchronized);
            return fixture;
        }

        public void Dispose()
        {
            this.Editor.Dispose();
            this.Messenger.Reset();
            this.container.Dispose();
            this.Files.Dispose();
        }

        private void CreateEditor(bool active, Oxygen.Editor.World.Workspace.PreviewSettingsService? previewSettings, Action? commitInput, bool synchronized)
        {
            var projects = new ProjectContextService();
            projects.Activate(this.Files.Context);
            this.container.RegisterInstance<IProjectContextService>(projects);
            _ = this.Sync.Setup(value => value.GetDocumentScene(this.Metadata.DocumentId)).Returns(this.Scene);
            _ = this.Sync.Setup(value => value.IsSceneSynchronized(this.Scene)).Returns(synchronized);
            _ = this.Prompt.Setup(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>())).Returns(Task.CompletedTask);
            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(active ? this.Metadata.DocumentId : Guid.NewGuid());
            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Callback(() => commitInput?.Invoke()).Returns(Task.CompletedTask);
            this.Editor = new(
                this.Metadata,
                documents.Object,
                this.Files.Documents,
                default,
                Mock.Of<IEngineService>(),
                this.Sync.Object,
                input.Object,
                this.Results,
                new OperationStatusReducer(),
                this.Commands.Object,
                Mock.Of<IContentPipelineService>(),
                this.container,
                this.Messenger,
                new SceneCookInputRegistrar(
                    new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(),
                    new DroidNet.Hosting.WinUI.HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! }),
                previewSettings ?? new Oxygen.Editor.World.Workspace.PreviewSettingsService(
                    Mock.Of<IEngineService>(),
                    Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(),
                    Mock.Of<IProjectContextService>(),
                    Mock.Of<IOperationResultPublisher>(),
                    new OperationStatusReducer()),
                conflictPrompt: this.Prompt.Object);
        }
    }

    private sealed class CapturingResults : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => System.Reactive.Disposables.Disposable.Empty;
    }
}
