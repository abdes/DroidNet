// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Routing;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures the Explorer's create, rename and paste actions change the scene document, one undo step each.</summary>
[TestClass]
public sealed partial class SceneExplorerAuthoringTests
{
    [TestMethod]
    public async Task AddEntity_AddsARootNodeInOneUndoStep()
    {
        using var harness = new AuthoringHarness(out var scene, out _);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = scene.RootNodes.Should().HaveCount(2);
        _ = harness.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task CreateFolder_AddsANewFolderInOneUndoStep()
    {
        using var harness = new AuthoringHarness(out var scene, out _);
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        await explorer.CreateFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = harness.Layout.Should().ContainSingle(entry => entry.FolderId != null && entry.Name == "New Folder");
        _ = harness.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public void RenameSceneRoot_AdapterPublishesLabelChangesUntilDetached()
    {
        using var harness = new AuthoringHarness(out var scene, out _);
        var adapter = new SceneAdapter(scene, harness.Layout);
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
        using var harness = new AuthoringHarness(out var scene, out _);
        var relocation = new Mock<IAssetRelocationService>();
        var histories = new Mock<Shell.Documents.IHistoryService>();
        var global = new HistoryKeeper(this);
        _ = histories.SetupGet(value => value.GlobalHistory).Returns(global);
        var contexts = new ProjectContextService();
        contexts.Activate(harness.Project);
        using var explorer = harness.Build(relocation.Object, histories.Object, contexts);
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var root = explorer.Scene!;
        explorer.SelectDisplayedItem(root, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);
        ITreeItem? requested = null;
        explorer.RenameRequested += (_, args) => requested = args?.Item;

        _ = explorer.RenameSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
        explorer.RenameSelectedCommand.Execute(parameter: null);
        _ = requested.Should().BeSameAs(root);
        var renamed = await explorer.CommitRenameAsync(root, "Lantern Demo").ConfigureAwait(false);

        _ = renamed.Succeeded.Should().BeTrue();
        relocation.Verify(
            value => value.RelocateAsync(
                It.Is<AssetRelocationRequest>(request => request.Moves.Single() == new AssetRelocationMove(AssetPath.Parse("/Content/Scene.oscene.json"), AssetPath.Parse("/Content/Lantern Demo.oscene.json"))),
                "Rename",
                global,
                It.IsAny<CancellationToken>()),
            Times.Once);
    }

    [TestMethod]
    public void BuildSelectionContext_ClassifiesRowKindsAndPrimary()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };
        var nodeAdapter = new SceneNodeAdapter(node);
        var folderAdapter = new FolderAdapter(FolderId.New(), "Folder");
        var sceneAdapter = new SceneAdapter(scene, []) { IsRoot = true };

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
    public async Task CopyItemsThenPasteItems_PastesACopyInOneUndoStep()
    {
        using var harness = new AuthoringHarness(out var scene, out var node);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = adapter.Should().NotBeNull();

        await explorer.CopyItemsAsync([adapter!]).ConfigureAwait(false);
        _ = explorer.CurrentClipboardState.Should().Be(ClipboardState.Copied, "copy should stage the node through the deep-copy clipboard");
        _ = explorer.PasteCommand.CanExecute(parameter: null).Should().BeTrue("copy should capture node ids for paste");

        await explorer.PasteItemsAsync(targetParent: explorer.Scene).ConfigureAwait(false);

        _ = scene.RootNodes.Should().HaveCount(2);
        _ = harness.History.UndoStack.Should().ContainSingle();
    }

    private sealed partial class AuthoringHarness : IDisposable
    {
        private readonly Scene scene;
        private readonly List<SceneTab> openDocuments = [];

        public AuthoringHarness(out Scene scene, out SceneNode node)
        {
            var project = new Project(new ProjectInfo("Authoring", Category.Games, "H:/AuthoringTreeTests", "preview.png"));
            var built = new Scene { Name = "Scene" };
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
        }

        public Mock<IProjectManagerService> Manager { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Editor.Documents.OpenDocuments OpenDocuments => this.openDocuments[0].Documents;

        /// <summary>Gets the project that holds the harness's first scene.</summary>
        public ProjectContext Project => this.openDocuments[0].Project;

        /// <summary>Gets the history of the harness's first scene.</summary>
        public HistoryKeeper History => this.openDocuments[0].Document.History;

        /// <summary>Gets the explorer layout of the harness's first scene.</summary>
        public IList<ExplorerEntryData> Layout => this.openDocuments[0].Layout;

        public void Dispose()
        {
            foreach (var tab in this.openDocuments)
            {
                tab.Dispose();
            }
        }

        public Scene AddScene(string name)
        {
            var added = new Scene { Name = name };
            var metadata = SceneTabs.Open(added);
            added = metadata.Scene;
            this.openDocuments.Add(metadata);
            this.Documents.Add(metadata.Document);
            return added;
        }

        public SceneExplorerViewModel Build(
            IAssetRelocationService? relocation = null,
            Shell.Documents.IHistoryService? histories = null,
            IProjectContextService? contexts = null)
            => new(
                this.Manager.Object,
                new StrongReferenceMessenger(),
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                default,
                new SceneSelectionService(),
                projectContexts: contexts,
                relocation: relocation,
                histories: histories);
    }
}
