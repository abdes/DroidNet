// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.TimeMachine;
using DryIoc;
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

/// <summary>Verifies how the scene editor saves, closes and follows its document's content.</summary>
[TestClass]
internal sealed partial class SceneEditorConflictTests
{
    /// <summary>Gets or sets the test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A reload after a save conflict gives the editor the scene on disk, which it saves again.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadAfterAConflictGivesTheEditorTheSceneOnDisk()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        await fixture.Editor.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeTrue();

        // The document service reloads the document; the editor follows the content it gets.
        var reload = await fixture.Editor.Document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reload.Succeeded.Should().BeTrue(reload.Problem);
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeFalse();
        var current = ((SceneContent)fixture.Editor.Document.Content).Scene;
        _ = current.Should().NotBeSameAs(fixture.Scene);
        _ = current!.RootNodes.Should().ContainSingle().Which.Name.Should().Be("Outside");
        _ = fixture.Editor.Document.IsDirty.Should().BeFalse();
        fixture.Editor.ResumeEditing();
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
    }

    /// <summary>A copy of a scene in conflict is a separate scene; the original stays dirty and in conflict.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SceneCopyRetainsOriginalDirtyStateAndConflict()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);
        _ = await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false);

        var copy = await fixture.Editor.Document.SaveCopyAsync(fixture.Files.FileOf("Copy"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copy.Succeeded.Should().BeTrue(copy.Message);
        _ = fixture.Files.Read("Copy").Name.Should().Be("Copy");
        _ = fixture.Editor.Document.IsDirty.Should().BeTrue();
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeTrue();
        _ = fixture.Editor.Document.File.Path.Should().Be(fixture.Files.PathOf("Main"));
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeTrue();
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
            var documents = new TestDocumentArea(this.Tab.Document);
            if (!active)
            {
                documents.Activate(document: null);
            }

            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync()).Callback(() => commitInput?.Invoke()).Returns(Task.CompletedTask);
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
                previewSettingsService);
        }
    }

    private sealed class CapturingResults : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => System.Reactive.Disposables.Disposable.Empty;
    }
}
