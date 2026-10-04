// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures drag/drop commits go through the document command owner and reconcile the projection.</summary>
[TestClass]
public sealed class SceneExplorerDropTests
{
    [TestMethod]
    public async Task CommitDropAsync_RoutesNodeDropIntoNodeThroughReparentCommand()
    {
        var harness = CreateHarness(out var scene, out var parent, out var child);
        var commands = harness.Commands;
        _ = commands
            .Setup(value => value.ReparentNodesAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), parent.Id, true))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var childAdapter = await explorer.FindAdapterByNodeIdAsync(child.Id).ConfigureAwait(false);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([childAdapter!], parentAdapter!, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Items.Should().ContainSingle().Which.Should().BeAssignableTo<SceneNodeAdapter>();
        commands.Verify(
            value => value.ReparentNodesAsync(
                It.IsAny<SceneDocumentCommandContext>(),
                It.Is<IReadOnlyList<Guid>>(ids => ids.SequenceEqual(new[] { child.Id })),
                parent.Id,
                true),
            Times.Once);
    }

    [TestMethod]
    public async Task CommitDropAsync_RoutesNodeDropIntoFolderThroughGroupCommand()
    {
        var harness = CreateHarness(out var scene, out var parent, out var child, folderId: Guid.NewGuid());
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData { Type = "Node", NodeId = parent.Id },
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = harness.FolderId,
                    Name = "Group",
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = child.Id }],
                },
            ]);
        var commands = harness.Commands;
        _ = commands
            .Setup(value => value.MoveNodesToFolderAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<Guid>>(), harness.FolderId))
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var folder = await explorer.FindFolderAdapterAsync(harness.FolderId).ConfigureAwait(false);
        var parentAdapter = await explorer.FindAdapterByNodeIdAsync(parent.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([parentAdapter!], folder!, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        commands.Verify(
            value => value.MoveNodesToFolderAsync(
                It.IsAny<SceneDocumentCommandContext>(),
                It.Is<IReadOnlyList<Guid>>(ids => ids.SequenceEqual(new[] { parent.Id })),
                harness.FolderId),
            Times.Once);
    }

    [TestMethod]
    public async Task CommitDropAsync_RejectsCopyDropWithoutCommand()
    {
        var harness = CreateHarness(out var scene, out _, out var child);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var childAdapter = await explorer.FindAdapterByNodeIdAsync(child.Id).ConfigureAwait(false);

        var result = await explorer.CommitDropAsync(new TreeDropRequest([childAdapter!], explorer.Scene!, 0, TreeDropOperation.Copy)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        harness.Commands.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task FindFolderAdapterAsync_ReturnsIndexedFolderWithoutForceLoading()
    {
        var harness = CreateHarness(out var scene, out var parent, out var child, folderId: Guid.NewGuid());
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = harness.FolderId,
                    Name = "Group",
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = parent.Id }],
                },
                new ExplorerEntryData { Type = "Node", NodeId = child.Id },
            ]);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(harness.FolderId).ConfigureAwait(false);

        _ = folder.Should().NotBeNull();
        _ = folder!.Id.Should().Be(harness.FolderId);
    }

    private static Harness CreateHarness(out Scene scene, out SceneNode parent, out SceneNode child, Guid? folderId = null)
    {
        var project = new Project(new ProjectInfo("Drop", Category.Games, "H:/DropTreeTests", "preview.png")) { Name = "Drop" };
        scene = new Scene(project) { Name = "Scene" };
        parent = new SceneNode(scene) { Name = "Parent" };
        child = new SceneNode(scene) { Name = "Child" };
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(child);
        project.Scenes.Add(scene);
        return new Harness(project, scene, folderId ?? Guid.NewGuid());
    }

    private sealed class Harness
    {
        public Harness(Project project, Scene scene, Guid folderId)
        {
            this.Project = project;
            this.Scene = scene;
            this.FolderId = folderId;
            this.Metadata = new SceneDocumentMetadata(scene.Id);

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns([this.Metadata]);
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(this.Metadata.DocumentId);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(this.Metadata)).Returns(scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), this.Metadata)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;

            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Project Project { get; }

        public Scene Scene { get; }

        public Guid FolderId { get; }

        public SceneDocumentMetadata Metadata { get; }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public SceneAdapter? SceneAdapter { get; private set; }

        public SceneExplorerViewModel Build()
        {
            var explorer = new SceneExplorerViewModel(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents.Object,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                this.Commands.Object);
            this.SceneAdapter = explorer.Scene;
            return explorer;
        }
    }
}
