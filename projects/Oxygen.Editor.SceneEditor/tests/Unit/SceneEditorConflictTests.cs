// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Settings;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneEditor.Unit.Tests.TestSupport;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

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
        _ = current!.RootNodes.Should().ContainSingle().Which.Metadata.Name.Should().Be("Outside");
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

        var copy = await fixture.Editor.Document.SaveCopyAsync(fixture.Scenes.FileOf("Copy"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copy.Succeeded.Should().BeTrue(copy.Message);
        _ = fixture.ReadScene("Copy").Name.Should().Be("Copy");
        _ = fixture.Editor.Document.IsDirty.Should().BeTrue();
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeTrue();
        _ = fixture.Editor.Document.File.Path.Should().Be(fixture.Scenes.FileOf("Main").Path);
        _ = (await fixture.Editor.SaveForCloseAsync().ConfigureAwait(false)).Should().BeFalse();
        _ = fixture.Editor.Document.IsChangedOnDisk.Should().BeTrue();
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly List<TestSession> sessions = [];

        // The preview preferences the fixture made because the test passed none.
        private PreviewSettingsService? ownedPreviewSettings;

        private Fixture(TestSceneDocuments scenes, Document document)
        {
            this.Scenes = scenes;
            this.Scene = ((SceneContent)document.Content).Scene;
            this.Tab = new DocumentTab(document, new DocumentEditorDescriptor(document.Kind, static _ => new object()) { IsClosable = false });
        }

        public TestSceneDocuments Scenes { get; }

        public Scene Scene { get; }

        public DocumentTab Tab { get; }

        public SceneEditorViewModel Editor { get; private set; } = null!;

        public Mock<IScenePreview> Preview { get; } = new();

        public CapturingResults Results { get; } = new();

        /// <summary>Opens "Main" with an unsaved edit after another program replaced its file, in a scene editor.</summary>
        /// <param name="test">The running test.</param>
        /// <param name="active">Whether the scene's tab is the active document.</param>
        /// <param name="previewSettings">The preview preferences, or a default service.</param>
        /// <param name="commitInput">Runs when the editor commits the focused control's input.</param>
        /// <param name="synchronized">Whether the scene is already in the engine.</param>
        /// <returns>The fixture.</returns>
        public static async Task<Fixture> CreateAsync(TestContext test, bool active, PreviewSettingsService? previewSettings = null, Action? commitInput = null, bool synchronized = false)
        {
            ArgumentNullException.ThrowIfNull(test);
            var scenes = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
            var id = SceneId.New();
            var file = scenes.Write(Data(id, "Main"));
            var document = await scenes.OpenAsync(file).ConfigureAwait(false);
            document.History.AddChange("Edit", static () => { });
            scenes.Write(file, Data(id, "Main", "Outside"));
            var fixture = new Fixture(scenes, document);
            fixture.CreateEditor(active, previewSettings, commitInput, synchronized);
            return fixture;
        }

        public void Dispose()
        {
            this.Editor.Dispose();
            this.ownedPreviewSettings?.Dispose();
            foreach (var session in Enumerable.Reverse(this.sessions))
            {
                session.Dispose();
            }

            this.Scenes.Dispose();
        }

        /// <summary>Reads what a scene file records.</summary>
        /// <param name="name">The scene's name, which names its file.</param>
        /// <returns>The scene the file records.</returns>
        public SceneData ReadScene(string name)
            => JsonSerializer.Deserialize(this.Scenes.Project.Context.FileSystem.File.ReadAllBytes(this.Scenes.FileOf(name).Path.FullPath), SceneJsonContext.Default.SceneData)!;

        // A scene file's content, its nodes complete with their default components.
        private static SceneData Data(SceneId id, string name, params string[] nodes)
        {
            var scene = new Scene { Name = name };
            foreach (var node in nodes)
            {
                scene.RootNodes.Add(new SceneNode(scene, node));
            }

            return scene.Dehydrate() with { Id = id };
        }

        private void CreateEditor(bool active, PreviewSettingsService? previewSettings, Action? commitInput, bool synchronized)
        {
            _ = this.Preview.Setup(value => value.IsReady(this.Tab.DocumentId)).Returns(synchronized);
            var documents = new TestDocumentArea(this.Tab.Document);
            if (!active)
            {
                documents.Activate(document: null);
            }

            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<DroidNet.Documents.DocumentId>())).Callback(() => commitInput?.Invoke()).Returns(Task.CompletedTask);
            var session = this.NewSession();
            if (previewSettings is null)
            {
                this.ownedPreviewSettings = TestSceneEditorServices.CreatePreviewSettings(session, this.Results);
                previewSettings = this.ownedPreviewSettings;
            }

            this.Editor = new(
                this.Tab,
                TestSceneEditorServices.Create(session, previewSettings, this.Preview.Object, input.Object, this.Results));
            this.Editor.AttachWindow(new DocumentWindowServices(new WindowId(1), documents, Mock.Of<ICommandService>(), Mock.Of<IPlacementService>(), Mock.Of<IAssetReveal>()));
        }

        private TestSession NewSession()
        {
            var session = new TestSession(this.Scenes.Project.Context);
            this.sessions.Add(session);
            return session;
        }
    }

    private sealed class CapturingResults : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => System.Reactive.Disposables.Disposable.Empty;
    }
}
