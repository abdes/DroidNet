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
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies scene conflict actions through the real document view model.</summary>
[TestClass]
internal sealed partial class SceneEditorConflictTests
{
    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A reload after a save conflict installs the scene on disk, ends the conflict and tells the views.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ConflictReloadInstallsTheSceneOnDisk()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        await fixture.Editor.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.HasSaveConflict.Should().BeTrue();

        var reload = await fixture.Editor.ReloadFromDiskAsync().ConfigureAwait(false);

        _ = reload.Succeeded.Should().BeTrue(reload.Message);
        _ = fixture.Editor.HasSaveConflict.Should().BeFalse();
        var current = ((SceneContent)fixture.Editor.Document.Content).Scene;
        _ = current.Should().NotBeSameAs(fixture.Scene);
        _ = current!.RootNodes.Should().ContainSingle().Which.Name.Should().Be("Outside");
        _ = fixture.Editor.Document.IsDirty.Should().BeFalse();
        fixture.Prompt.Verify(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>()), Times.Never);
        fixture.Editor.ResumeEditing();
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
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
        var read = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Store.BeforeRead = _ => read.Task;
        var reload = fixture.Editor.ReloadFromDiskAsync();
        var close = fixture.Editor.CloseAsync(discard: true);

        _ = close.IsCompleted.Should().BeFalse();
        read.SetResult();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await close.ConfigureAwait(false);
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly Container container = new();

        private Fixture(GatedFileStore store, SceneDocumentFiles files, Document document)
        {
            this.Store = store;
            this.Files = files;
            this.Scene = ((SceneContent)document.Content).Scene;
            this.Tab = new DocumentTab(document, new DocumentEditorDescriptor(document.Kind, static _ => new object()) { IsClosable = false });
        }

        public SceneDocumentFiles Files { get; }

        public Scene Scene { get; }

        public DocumentTab Tab { get; }

        public SceneEditorViewModel Editor { get; private set; } = null!;

        public StrongReferenceMessenger Messenger { get; } = new();

        public Mock<IScenePreview> Preview { get; } = new();

        public GatedFileStore Store { get; }

        public Mock<IDocumentConflictPrompt> Prompt { get; } = new();

        public CapturingResults Results { get; } = new();

        /// <summary>Opens "Main" with an unsaved edit after another program replaced its file, in a scene editor.</summary>
        /// <param name="test">The running test.</param>
        /// <param name="active">Whether the scene's tab is the active document.</param>
        /// <param name="previewSettings">The preview preferences, or a default service.</param>
        /// <param name="commitInput">Runs when the editor commits the focused control's input.</param>
        /// <param name="synchronized">Whether the scene is already in the engine.</param>
        /// <returns>The fixture.</returns>
        public static async Task<Fixture> CreateAsync(TestContext test, bool active, World.Workspace.PreviewSettingsService? previewSettings = null, Action? commitInput = null, bool synchronized = false)
        {
            var files = new SceneDocumentFiles();
            var id = SceneId.New();
            files.Write("Main", SceneDocumentFiles.Data(id, "Main"));
            var (document, _) = await files.OpenAsync("Main", test.CancellationToken).ConfigureAwait(false);
            document.History.AddChange("Edit", static () => { });
            files.Write("Main", SceneDocumentFiles.Data(id, "Main", "Outside"));
            var fixture = new Fixture(files.Store, files, document);
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

        private void CreateEditor(bool active, World.Workspace.PreviewSettingsService? previewSettings, Action? commitInput, bool synchronized)
        {
            var projects = new ProjectContextService();
            projects.Activate(this.Files.Context);
            this.container.RegisterInstance<IProjectContextService>(projects);
            _ = this.Preview.Setup(value => value.IsReady(this.Tab.DocumentId)).Returns(synchronized);
            _ = this.Prompt.Setup(value => value.ShowAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<IDocumentConflictParticipant>())).Returns(Task.CompletedTask);
            var documents = new TestDocumentArea(this.Tab.Document);
            if (!active)
            {
                documents.Activate(document: null);
            }

            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Callback(() => commitInput?.Invoke()).Returns(Task.CompletedTask);
            var previewSettingsService = previewSettings ?? new World.Workspace.PreviewSettingsService(
                    Mock.Of<IEngineService>(),
                    Mock.Of<Data.Services.IEditorSettingsManager>(),
                    Mock.Of<IProjectContextService>(),
                    Mock.Of<IOperationResultPublisher>(),
                    new OperationStatusReducer());
            this.Editor = new(
                this.Tab,
                documents,
                default,
                Mock.Of<IEngineService>(),
                this.Preview.Object,
                input.Object,
                this.Results,
                new OperationStatusReducer(),
                Mock.Of<IContentPipelineService>(),
                this.container,
                this.Messenger,
                previewSettingsService,
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
