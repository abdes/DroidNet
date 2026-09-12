// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
public sealed class SceneSaveRevisionTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task AutomaticCookIsNotifiedOnlyAfterSuccessfulSave(bool failWrite)
    {
        var fixture = new SaveFixture { FailWrite = failWrite };
        var save = fixture.Commands.SaveSceneAsync(fixture.Context);
        await fixture.WriteStarted.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.AutomaticCooking.VerifyNoOtherCalls();
        fixture.ReleaseWrite.SetResult();
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().Be(!failWrite);
        fixture.AutomaticCooking.Verify(value => value.NotifySaved(It.IsAny<string>(), "saved snapshot", contentChanged: true), failWrite ? Times.Never : Times.Once);
        if (!failWrite)
        {
            _ = await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(false);
            fixture.AutomaticCooking.Verify(value => value.NotifySaved(It.IsAny<string>(), "saved snapshot", contentChanged: false), Times.Once);
        }
    }

    [TestMethod]
    public async Task HierarchyAddRecordsRevisionBeforeAwaitingLiveSync()
    {
        var fixture = new SaveFixture();
        var save = fixture.Commands.SaveSceneAsync(fixture.Context);
        await fixture.WriteStarted.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var sync = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), parentGuid: null)).Returns(sync.Task);
        var service = MakeHierarchyService(fixture);
        service.AuthoringChanged += (_, _) => fixture.Context.Metadata.IsDirty = true;
        var edit = service.AddNodeAsync(new SceneAdapter(fixture.Context.Scene), new SceneNode(fixture.Context.Scene) { Name = "Tree Node" });
        _ = edit.IsCompleted.Should().BeFalse();
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(2);
        fixture.ReleaseWrite.SetResult();
        _ = (await save.ConfigureAwait(false)).HasUnsavedChanges.Should().BeTrue();
        _ = fixture.Persisted.Should().NotContain("Tree Node");
        sync.SetResult();
        _ = await edit.ConfigureAwait(false);
    }

    [TestMethod]
    public async Task HierarchyDeleteCommitsNodesAndLayoutBeforeAwaitingLiveSync()
    {
        var fixture = new SaveFixture();
        var scene = fixture.Context.Scene;
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        scene.SetExplorerLayout([new ExplorerEntryData { NodeId = first.Id }, new ExplorerEntryData { NodeId = second.Id }]);
        var sync = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Sync.Setup(value => value.RemoveNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>())).Returns(sync.Task);
        var service = MakeHierarchyService(fixture);
        service.AuthoringChanged += (_, _) => fixture.Context.Metadata.IsDirty = true;
        var removal = service.DeleteItemsAsync([new SceneNodeAdapter(first), new SceneNodeAdapter(second)]);
        _ = removal.IsCompleted.Should().BeFalse();
        _ = scene.RootNodes.Should().BeEmpty();
        _ = scene.ExplorerLayout.Should().BeEmpty();
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(3);
        sync.SetResult();
        _ = await removal.ConfigureAwait(false);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task EditDuringSavePreservesNewerRevision(bool failWrite)
    {
        var fixture = new SaveFixture { FailWrite = failWrite };
        var save = fixture.Commands.SaveSceneAsync(fixture.Context);
        await fixture.WriteStarted.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var sync = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), parentGuid: null)).Returns(sync.Task);
        var edit = fixture.Commands.CreatePrimitiveAsync(fixture.Context, "Cube");
        _ = edit.IsCompleted.Should().BeFalse();
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(2);
        fixture.ReleaseWrite.SetResult();
        var result = await save.ConfigureAwait(false);
        _ = result.Succeeded.Should().Be(!failWrite);
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
        _ = fixture.Context.Metadata.SavedVersion.Should().Be(failWrite ? 0 : 1);
        _ = fixture.Context.Scene.RootNodes.Should().ContainSingle();
        _ = fixture.Persisted.Should().NotContain("Cube");
        sync.SetResult();
        _ = (await edit.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(2);
        fixture.FailWrite = false;
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Persisted.Should().Contain("Cube");
    }

    [TestMethod]
    public async Task OverlappingSavesCannotCommitAnOlderSnapshotLast()
    {
        var fixture = new SaveFixture();
        var first = fixture.Commands.SaveSceneAsync(fixture.Context);
        await fixture.WriteStarted.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await fixture.Commands.CreatePrimitiveAsync(fixture.Context, "Cube").ConfigureAwait(false);
        var second = fixture.Commands.SaveSceneAsync(fixture.Context);
        _ = fixture.Writes.Should().Be(1);
        fixture.ReleaseWrite.SetResult();
        _ = (await Task.WhenAll(first, second).ConfigureAwait(false)).Should().OnlyContain(result => result.Succeeded);
        _ = fixture.Writes.Should().Be(2);
        _ = fixture.Persisted.Should().Contain("Cube");
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task SnapshotIsFrozenBeforeFolderResolutionIncludingMutableLayout()
    {
        var fixture = new SaveFixture();
        var folder = new ExplorerEntryData { Type = "Folder", Name = "Before", Children = [] };
        fixture.Context.Scene.SetExplorerLayout([folder]);
        var folderReady = new TaskCompletionSource<IFolder>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Storage.Setup(value => value.GetFolderFromPathAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).Returns(folderReady.Task);
        var save = fixture.Commands.SaveSceneAsync(fixture.Context);
        folder.Name = "After";
        folder.EnsureChildren().Add(new ExplorerEntryData { Type = "Folder", Name = "New child" });
        fixture.Context.Metadata.IsDirty = true;
        fixture.ReleaseWrite.SetResult();
        folderReady.SetResult(fixture.Folder);
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = fixture.Persisted.Should().Contain("Before").And.NotContain("After").And.NotContain("New child");
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
        _ = folder.Name.Should().Be("After");
    }

    [TestMethod]
    public async Task MultiNodeEditCommitsAllModelsAndRevisionBeforeLiveSync()
    {
        var fixture = new SaveFixture();
        var scene = fixture.Context.Scene;
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sync = new TaskCompletionSource<SyncOutcome>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Sync.Setup(value => value.UpdatePropertiesAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>())).Returns(sync.Task);
        var edit = fixture.Commands.EditTransformAsync(fixture.Context, [first.Id, second.Id], new TransformEdit(default, default, default, PositionX: OptionalEditValues.Supplied(42f)), EditSessionToken.OneShot);
        _ = edit.IsCompleted.Should().BeFalse();
        _ = first.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(42);
        _ = second.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(42);
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(2);
        fixture.ReleaseWrite.SetResult();
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        sync.SetResult(new SyncOutcome(SyncStatus.Accepted, "Edit", AffectedScope.Empty));
        _ = (await edit.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    }

    private static SceneExplorerService MakeHierarchyService(SaveFixture fixture)
        => new(new SceneMutator(NullLogger<SceneMutator>.Instance), new SceneOrganizer(NullLogger<SceneOrganizer>.Instance), fixture.Sync.Object, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer());

    private sealed class SaveFixture
    {
        public SaveFixture()
        {
            var info = Mock.Of<IProjectInfo>(value => value.Location == "H:/SceneRevisionTests");
            var scene = new Scene(Mock.Of<IProject>(value => value.ProjectInfo == info)) { Name = "Saved Scene" };
            this.Context = new SceneDocumentCommandContext(scene.Id, new SceneDocumentMetadata(scene.Id) { IsDirty = true }, scene, new HistoryKeeper(scene));
            var document = new Mock<IDocument>();
            _ = document.SetupGet(value => value.Location).Returns("H:/SceneRevisionTests/Content/Scenes/Saved Scene.oscene.json");
            var atomicFiles = new Mock<IAtomicFileStore>();
            _ = atomicFiles.Setup(value => value.WriteAsync(It.IsAny<string>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<FileVersion>(), It.IsAny<CancellationToken>()))
                .Returns(async (string _, ReadOnlyMemory<byte> bytes, FileVersion _, CancellationToken _) =>
                {
                    await this.WriteAsync(Encoding.UTF8.GetString(bytes.Span)).ConfigureAwait(false);
                    return new FileVersion(Exists: true, "saved snapshot");
                });
            _ = this.Storage.SetupGet(value => value.AtomicFiles).Returns(atomicFiles.Object);
            var folder = new Mock<IFolder>();
            _ = folder.Setup(value => value.GetFolderAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync(folder.Object);
            _ = folder.Setup(value => value.ExistsAsync()).ReturnsAsync(value: true);
            _ = folder.Setup(value => value.GetDocumentAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync(document.Object);
            this.Folder = folder.Object;
            _ = this.Storage.Setup(value => value.GetFolderFromPathAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync(folder.Object);
            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
            this.Commands = new SceneDocumentCommandService(
                this.AutomaticCooking.Object,
                Mock.Of<ISceneExplorerService>(),
                new SceneSelectionService(),
                this.Sync.Object,
                new ProjectManagerService(this.Storage.Object),
                documents.Object,
                default,
                new StrongReferenceMessenger(),
                Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer());
        }

        public SceneDocumentCommandContext Context { get; }

        public Mock<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService> AutomaticCooking { get; } = new();

        public SceneDocumentCommandService Commands { get; }

        public Mock<IStorageProvider> Storage { get; } = new();

        public Mock<ISceneEngineSync> Sync { get; } = new();

        public IFolder Folder { get; }

        public TaskCompletionSource WriteStarted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseWrite { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int Writes { get; private set; }

        public string Persisted { get; private set; } = "previous saved scene";

        public bool FailWrite { get; set; }

        private async Task WriteAsync(string json)
        {
            ++this.Writes;
            _ = this.WriteStarted.TrySetResult();
            await this.ReleaseWrite.Task.ConfigureAwait(false);
            if (this.FailWrite)
            {
                throw new IOException("Injected scene write failure");
            }

            this.Persisted = json;
        }
    }
}
