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
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures create and delete authoring go through the document command owner.</summary>
[TestClass]
public sealed partial class SceneExplorerAuthoringTests
{
    [TestMethod]
    public async Task AddEntity_RoutesThroughCreateNodeCommand()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()))
            .ReturnsAsync(SceneCommandResults.Success(node));
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        harness.Commands.Verify(
            value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()),
            Times.Once);
    }

    [TestMethod]
    public async Task CreateFolder_RoutesThroughCreateFolderCommand()
    {
        var harness = new AuthoringHarness(out var scene, out _);
        _ = harness.Commands
            .Setup(value => value.CreateFolderAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, "New Folder"))
            .ReturnsAsync(SceneCommandResults.Success(Guid.NewGuid()));
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.CreateFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        harness.Commands.Verify(
            value => value.CreateFolderAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, "New Folder"),
            Times.Once);
    }

    [TestMethod]
    public void RenameSceneRoot_AdapterPublishesLabelChangesUntilDetached()
    {
        _ = new AuthoringHarness(out var scene, out _);
        var adapter = new SceneAdapter(scene);
        var notifications = new List<string?>();
        adapter.PropertyChanged += (_, args) => notifications.Add(args.PropertyName);

        scene.Name = "Renamed";

        _ = adapter.Label.Should().Be("Renamed");
        _ = notifications.Should().Equal(nameof(SceneAdapter.Label), nameof(SceneAdapter.DisplayLabel));
        adapter.Detach();
        scene.Name = "Detached";
        _ = notifications.Should().Equal(nameof(SceneAdapter.Label), nameof(SceneAdapter.DisplayLabel));
    }

    [TestMethod]
    public async Task RenameSceneRoot_ToolbarAndInlineUseDocumentRenameCommand()
    {
        var harness = new AuthoringHarness(out var scene, out _);
        _ = harness.Commands.Setup(value => value.RenameSceneAsync(It.IsAny<SceneDocumentCommandContext>(), "Lantern Demo"))
            .Callback<SceneDocumentCommandContext, string>((context, name) => context.Scene.Name = name)
            .ReturnsAsync(SceneCommandResult.Success);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var root = explorer.Scene!;
        explorer.SelectDisplayedItem(root, explorer.ShownItems.ToArray(), isControlDown: false, isShiftDown: false);
        ITreeItem? requested = null;
        explorer.RenameRequested += (_, args) => requested = args?.Item;

        _ = explorer.RenameSelectedCommand.CanExecute(null).Should().BeTrue();
        explorer.RenameSelectedCommand.Execute(null);
        _ = requested.Should().BeSameAs(root);
        var renamed = await explorer.CommitRenameAsync(root, "Lantern Demo").ConfigureAwait(false);

        _ = renamed.Succeeded.Should().BeTrue();
        _ = root.Label.Should().Be("Lantern Demo");
        harness.Commands.Verify(value => value.RenameSceneAsync(It.IsAny<SceneDocumentCommandContext>(), "Lantern Demo"), Times.Once);
    }

    [TestMethod]
    public void BuildSelectionContext_ClassifiesRowKindsAndPrimary()
    {
        var project = new Project(new ProjectInfo("Sel", Category.Games, "H:/SelTests", "preview.png")) { Name = "Sel" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        var nodeAdapter = new SceneNodeAdapter(node);
        var folderAdapter = new FolderAdapter(Guid.NewGuid(), "Folder");
        var sceneAdapter = new SceneAdapter(scene) { IsRoot = true };

        _ = SceneExplorerViewModel.BuildSelectionContext([nodeAdapter]).Kind.Should().Be(SceneSelectionKind.Node);
        _ = SceneExplorerViewModel.BuildSelectionContext([folderAdapter]).Kind.Should().Be(SceneSelectionKind.Folder);
        _ = SceneExplorerViewModel.BuildSelectionContext([sceneAdapter]).Kind.Should().Be(SceneSelectionKind.Scene);
        _ = SceneExplorerViewModel.BuildSelectionContext([nodeAdapter, folderAdapter]).Kind.Should().Be(SceneSelectionKind.Mixed);
        _ = SceneExplorerViewModel.BuildSelectionContext([]).Kind.Should().Be(SceneSelectionKind.Empty);

        var nodeContext = SceneExplorerViewModel.BuildSelectionContext([nodeAdapter]);
        _ = nodeContext.PrimaryNodeId.Should().Be(node.Id);
        _ = nodeContext.PrimaryFolderId.Should().BeNull();
    }

    [TestMethod]
    public async Task CopyItemsThenPasteItems_RoutesThroughDeepCopyCommand()
    {
        var harness = new AuthoringHarness(out var scene, out var node);
        _ = harness.Commands
            .Setup(value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null))
            .ReturnsAsync(SceneCommandResults.Success<IReadOnlyList<SceneNode>>([node]));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();

        await explorer.CopyItemsAsync([adapter!]).ConfigureAwait(false);
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Copied, "copy should stage the node through the deep-copy clipboard");
        _ = explorer.PasteCommand.CanExecute(null).Should().BeTrue("copy should capture node ids for paste");

        await explorer.PasteItemsAsync(targetParent: explorer.Scene).ConfigureAwait(false);

        harness.Commands.Verify(
            value => value.DuplicateNodesFromDataAsync(It.IsAny<SceneDocumentCommandContext>(), It.IsAny<IReadOnlyList<SceneNodeData>>(), null, null, null),
            Times.Once);
    }

    private sealed class AuthoringHarness
    {
        private readonly Scene scene;
        private readonly List<SceneDocumentMetadata> openDocuments = [];

        public AuthoringHarness(out Scene scene, out SceneNode node)
        {
            var project = new Project(new ProjectInfo("Authoring", Category.Games, "H:/AuthoringTreeTests", "preview.png")) { Name = "Authoring" };
            scene = new Scene(project) { Name = "Scene" };
            node = new SceneNode(scene) { Name = "Node" };
            scene.RootNodes.Add(node);
            project.Scenes.Add(scene);
            this.scene = scene;

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new Mock<IDocumentService>();
            var metadata = new SceneDocumentMetadata(scene.Id);
            this.openDocuments.Add(metadata);
            _ = documents.Setup(value => value.GetOpenDocuments(It.IsAny<WindowId>())).Returns(() => this.openDocuments);
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(metadata.DocumentId);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(metadata)).Returns(scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;

            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public Scene AddScene(string name)
        {
            var added = new Scene(this.scene.Project) { Name = name };
            this.scene.Project.Scenes.Add(added);
            var metadata = new SceneDocumentMetadata(added.Id);
            this.openDocuments.Add(metadata);
            _ = this.Sync.Setup(value => value.GetDocumentScene(metadata)).Returns(added);
            _ = this.Sync.Setup(value => value.RegisterDocument(added, metadata)).Returns(value: true);
            return added;
        }

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents.Object,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                this.Commands.Object);
    }
}
