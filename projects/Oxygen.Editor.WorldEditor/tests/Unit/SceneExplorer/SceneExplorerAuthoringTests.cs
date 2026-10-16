// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;

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
            .ReturnsAsync(SceneCommandResults.Success(FolderId.New()));
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

    /// <summary>Renaming the scene root renames its file through the relocation service, in the global history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenameSceneRoot_ToolbarAndInlineRenameTheSceneFile()
    {
        var harness = new AuthoringHarness(out var scene, out _);
        var relocation = new Mock<IAssetRelocationService>();
        var histories = new Mock<Oxygen.Editor.Shell.Documents.IHistoryService>();
        var global = new HistoryKeeper(this);
        _ = histories.SetupGet(value => value.GlobalHistory).Returns(global);
        var contexts = new ProjectContextService();
        contexts.Activate(SceneTabs.Project);
        using var explorer = harness.Build(relocation.Object, histories.Object, contexts);
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
        relocation.Verify(
            value => value.RelocateAsync(
                It.Is<AssetRelocationRequest>(request => request.Moves.Single() == new AssetRelocationMove("/Content/Scene.oscene.json", "/Content/Lantern Demo.oscene.json")),
                "Rename",
                global,
                It.IsAny<CancellationToken>()),
            Times.Once);
    }

    [TestMethod]
    public void BuildSelectionContext_ClassifiesRowKindsAndPrimary()
    {
        var project = new Project(new ProjectInfo("Sel", Category.Games, "H:/SelTests", "preview.png")) { Name = "Sel" };
        var scene = new Scene(project) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        var nodeAdapter = new SceneNodeAdapter(node);
        var folderAdapter = new FolderAdapter(FolderId.New(), "Folder");
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
        private readonly List<SceneTab> openDocuments = [];

        public AuthoringHarness(out Scene scene, out SceneNode node)
        {
            var project = new Project(new ProjectInfo("Authoring", Category.Games, "H:/AuthoringTreeTests", "preview.png")) { Name = "Authoring" };
            var built = new Scene(project) { Name = "Scene" };
            var builtNode = new SceneNode(built) { Name = "Node" };
            built.RootNodes.Add(builtNode);

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new TestDocumentArea();
            var metadata = SceneTabs.Open(built);
            scene = metadata.Scene;
            node = metadata.NodeOf(builtNode);
            this.scene = scene;
            this.openDocuments.Add(metadata);
            documents.Activate(metadata.Document);
            this.Documents = documents;

            var sync = new Mock<ISceneEngineSync>();
            _ = sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(scene);
            _ = sync.Setup(value => value.RegisterDocument(It.IsAny<Scene>(), metadata.DocumentId)).Returns(value: true);
            _ = sync.Setup(value => value.SyncSceneWhenReadyAsync(It.IsAny<Scene>(), It.IsAny<CancellationToken>())).ReturnsAsync(value: false);
            this.Sync = sync;

            this.Commands = new Mock<ISceneDocumentCommandService>(MockBehavior.Strict);
        }

        public Mock<IProjectManagerService> Manager { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Oxygen.Editor.Documents.OpenDocuments OpenDocuments => this.openDocuments[0].Documents;

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public Scene AddScene(string name)
        {
            var added = new Scene(this.scene.Project) { Name = name };
            var metadata = SceneTabs.Open(added);
            added = metadata.Scene;
            this.openDocuments.Add(metadata);
            this.Documents.Add(metadata.Document);
            _ = this.Sync.Setup(value => value.GetDocumentScene(metadata.DocumentId)).Returns(added);
            _ = this.Sync.Setup(value => value.RegisterDocument(added, metadata.DocumentId)).Returns(value: true);
            return added;
        }

        public SceneExplorerViewModel Build(
            IAssetRelocationService? relocation = null,
            Oxygen.Editor.Shell.Documents.IHistoryService? histories = null,
            IProjectContextService? contexts = null)
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                default,
                this.Sync.Object,
                new SceneSelectionService(),
                this.Commands.Object,
                projectContexts: contexts,
                relocation: relocation,
                histories: histories);
    }
}
