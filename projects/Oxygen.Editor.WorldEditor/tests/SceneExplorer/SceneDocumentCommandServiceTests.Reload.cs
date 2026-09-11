// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Testably.Abstractions;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Exercises confirmed reload with real storage and the document's revision and lifetime.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>Accepted reload installs a new model, discards history and adopts the exact disk baseline.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadInstallsCurrentDiskSceneAndRetiresTheOriginalContext()
    {
        using var fixture = await ReloadFixture.CreateAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.WriteExternalAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = await fixture.Commands.ReloadSceneAsync(fixture.Context, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().NotBeSameAs(fixture.Scene);
        _ = fixture.Current.Should().BeSameAs(result.Value);
        _ = fixture.Scene.Project.ActiveScene.Should().BeSameAs(result.Value);
        _ = result.Value!.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(4);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = SceneAuthoringGate.TryEnter(fixture.Scene).Should().BeNull();
        _ = (await fixture.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(result.Value)).ConfigureAwait(false)).Should().BeTrue();
    }

    /// <summary>A newer revision or a closed lifetime cannot adopt a read or discard the old history.</summary>
    /// <param name="close">Whether to close the lifetime instead of advancing its revision.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ReloadRejectsAChangedOrClosedDocumentAfterRead(bool close)
    {
        using var fixture = await ReloadFixture.CreateAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.WriteExternalAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Read = async (scene, token) =>
        {
            var read = await fixture.Manager.ReadSceneForReloadAsync(scene, token).ConfigureAwait(false);
            if (close)
            {
                fixture.Current = null;
            }
            else
            {
                fixture.Context.Metadata.IsDirty = true;
            }

            return read;
        };

        var result = await fixture.Commands.ReloadSceneAsync(fixture.Context, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        await fixture.AssertOriginalRetainedAsync().ConfigureAwait(false);
    }

    /// <summary>Cancelling after reading does not acknowledge the new baseline or retire the model.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadCancellationAfterReadRetainsAuthoringAndBaseline()
    {
        using var fixture = await ReloadFixture.CreateAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.WriteExternalAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        fixture.Read = async (scene, token) =>
        {
            var read = await fixture.Manager.ReadSceneForReloadAsync(scene, token).ConfigureAwait(false);
            await cancellation.CancelAsync().ConfigureAwait(false);
            return read;
        };

        var result = await fixture.Commands.ReloadSceneAsync(fixture.Context, cancellation.Token).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.FailureMessage.Should().Contain("cancelled");
        await fixture.AssertOriginalRetainedAsync().ConfigureAwait(false);
        using var edit = SceneAuthoringGate.TryEnter(fixture.Scene);
        _ = edit.Should().NotBeNull();
    }

    /// <summary>A publication failure after acceptance still returns the new authoring model to its owner.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadPublicationFailureStillReturnsTheAcceptedModel()
    {
        using var fixture = await ReloadFixture.CreateAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.DocumentService.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()))
            .ThrowsAsync(new IOException("Notification failed"));

        var result = await fixture.Commands.ReloadSceneAsync(fixture.Context, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().BeSameAs(fixture.Current);
        _ = result.OperationResultId.Should().NotBeNull();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    }

    /// <summary>Reload cannot clear a still-open history batch.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadRejectsBusyHistoryBeforeReading()
    {
        using var fixture = await ReloadFixture.CreateAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var original = fixture.Context.History.UndoStack.Single();
        fixture.Context.History.BeginChangeSet("Pending batch");
        var result = await fixture.Commands.ReloadSceneAsync(fixture.Context, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Context.History.EndChangeSet();

        _ = result.Succeeded.Should().BeFalse();
        _ = fixture.ReadCount.Should().Be(0);
        _ = fixture.Context.History.UndoStack.Should().Contain(original);
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
    }

    private sealed partial class ReloadFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenSceneReload-");

        private ReloadFixture()
        {
            var project = new Project(new ProjectInfo("Reload", Category.Games, this.directory.FullName, "preview.png")) { Name = "Reload" };
            this.Scene = new Scene(project) { Name = "Main" };
            this.Scene.RootNodes.Add(new SceneNode(this.Scene) { Name = "Node" });
            project.Scenes.Add(this.Scene);
            project.ActiveScene = this.Scene;
            this.Current = this.Scene;
            this.Context = CreateContext(this.Scene);
            this.Manager = new(new NativeStorageProvider(new RealFileSystem()));
            this.Read = this.Manager.ReadSceneForReloadAsync;
            var sync = new Mock<ISceneEngineSync>(MockBehavior.Strict);
            _ = sync.Setup(value => value.GetDocumentScene(this.Context.Metadata)).Returns(() => this.Current);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), this.Context.Metadata)).Returns((Scene scene, SceneDocumentMetadata _) =>
            {
                this.Current = scene;
                return true;
            });
            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.Setup(value => value.ReadSceneForReloadAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).Returns((Scene scene, CancellationToken token) =>
            {
                this.ReadCount++;
                return this.Read(scene, token);
            });
            _ = manager.Setup(value => value.AcceptSceneReload(It.IsAny<SceneReloadSnapshot>())).Returns((SceneReloadSnapshot snapshot) => this.Manager.AcceptSceneReload(snapshot));
            var fixture = CreateFixture(sync.Object, manager.Object);
            this.Commands = fixture.Sut;
            this.DocumentService = fixture.DocumentService;
        }

        public Scene Scene { get; }

        public Scene? Current { get; set; }

        public SceneDocumentCommandContext Context { get; }

        public ProjectManagerService Manager { get; }

        public SceneDocumentCommandService Commands { get; }

        public Mock<IDocumentService> DocumentService { get; }

        public Func<Scene, CancellationToken, Task<SceneReloadSnapshot?>> Read { get; set; }

        public int ReadCount { get; private set; }

        public static async Task<ReloadFixture> CreateAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var fixture = new ReloadFixture();
            _ = await fixture.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(fixture.Scene)).ConfigureAwait(false);
            fixture.Context.Metadata.IsDirty = true;
            fixture.Context.History.AddChange("Unsaved edit", () => { });
            fixture.Scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(9, 0, 0);
            return fixture;
        }

        public Task WriteExternalAsync(CancellationToken cancellationToken)
        {
            var external = Scene.CreateAndHydrate(this.Scene.Project, this.Scene.Dehydrate());
            external.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(4, 0, 0);
            var path = Path.Combine(this.directory.FullName, "Content", "Scenes", "Main.oscene.json");
            return File.WriteAllTextAsync(path, SceneSaveSnapshot.Capture(external).Json, cancellationToken);
        }

        public async Task AssertOriginalRetainedAsync()
        {
            _ = this.Scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(this.Scene);
            _ = this.Scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(9);
            _ = this.Context.Metadata.IsDirty.Should().BeTrue();
            _ = this.Context.History.UndoStack.Should().ContainSingle();
            var save = () => this.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(this.Scene));
            _ = await save.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
        }

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
