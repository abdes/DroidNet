// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>
/// C16-C19/T5: the Explorer publishes the full classified selection (row kinds, ordered
/// identities, explicit primary), reconciles foreign selections by identity, restores the
/// selection across projection rebuilds and scene switches, and keeps the root row exclusive.
/// </summary>
[TestClass]
public sealed partial class SceneExplorerSelectionTests
{
    [TestMethod]
    public async Task FolderOnlySelection_PublishesFolderKindAndTypedContext()
    {
        using var harness = new SelectionHarness(out var scene, out _, out var folderId);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        explorer.SelectDisplayedItem(folder!, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);

        var context = harness.SelectionService.GetContext(harness.DocumentIdOf(scene));
        _ = context.Kind.Should().Be(SceneSelectionKind.Folder);
        _ = context.SelectedNodeIds.Should().BeEmpty("a folder selection must not claim its contained nodes");
        _ = context.SelectedFolderIds.Should().Equal(folderId);
        _ = context.PrimaryFolderId.Should().Be(folderId);
        var change = harness.Received[^1];
        _ = change.Context.Kind.Should().Be(SceneSelectionKind.Folder);
        _ = harness.SelectionService.GetSelectedNodes(change.DocumentId, scene).Should().BeEmpty();
    }

    [TestMethod]
    public async Task NodeAndFolderBatch_PublishesMixedKindWithBothIdentities()
    {
        using var harness = new SelectionHarness(out var scene, out var freeNode, out var folderId);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(freeNode.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(folder!, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);
        explorer.SelectDisplayedItem(nodeAdapter!, [.. explorer.ShownItems], isControlDown: true, isShiftDown: false);

        var context = harness.SelectionService.GetContext(harness.DocumentIdOf(scene));
        _ = context.Kind.Should().Be(SceneSelectionKind.Mixed);
        _ = context.SelectedNodeIds.Should().Equal(freeNode.Id);
        _ = context.SelectedFolderIds.Should().Equal(folderId);
        _ = context.PrimaryNodeId.Should().Be(freeNode.Id, "the explicitly active row is the primary, not the last in row order");
    }

    [TestMethod]
    public async Task RootRowSelection_IsExclusiveAndClassifiesScene()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(nodeAdapter!, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);

        var root = explorer.ShownItems.OfType<SceneAdapter>().Single();
        explorer.SelectDisplayedItem(root, [.. explorer.ShownItems], isControlDown: true, isShiftDown: false);

        _ = explorer.SelectedItemsCount.Should().Be(1, "the root row never shares a selection");
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(scene)).Kind.Should().Be(SceneSelectionKind.Scene);

        // And the reverse direction: with the root selected, ctrl-clicking a node replaces it.
        explorer.SelectDisplayedItem(nodeAdapter!, [.. explorer.ShownItems], isControlDown: true, isShiftDown: false);

        _ = explorer.SelectedItemsCount.Should().Be(1);
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(scene)).Kind.Should().Be(SceneSelectionKind.Node);
    }

    [TestMethod]
    public async Task ExternalSelectionByViewport_ReselectsExplorerRowsAndRepublishes()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        harness.SelectionService.Publish(
            harness.DocumentIdOf(scene),
            new SceneSelectionContext(SceneSelectionKind.Node, [node.Id], [], node.Id, PrimaryFolderId: null),
            "Viewport");

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(nodeAdapter, "foreign selections reach the rows by identity");
        _ = explorer.ActiveItem.Should().BeSameAs(nodeAdapter);
        var context = harness.SelectionService.GetContext(harness.DocumentIdOf(scene));
        _ = context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = context.PrimaryNodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public async Task ViewportSelection_ScrollsTheActiveRowIntoView()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var revealed = new List<ITreeItem>();
        explorer.BringIntoViewRequested += (_, args) => revealed.Add(args.TreeItem);

        harness.SelectionService.Publish(
            harness.DocumentIdOf(scene),
            new SceneSelectionContext(SceneSelectionKind.Node, [node.Id], [], node.Id, PrimaryFolderId: null),
            "Viewport");

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = revealed.Should().Equal(nodeAdapter!);
    }

    [TestMethod]
    public async Task InvokingANodeRow_AsksToFrameTheNode()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        explorer.InvokeItem(nodeAdapter!);

        harness.Views.Verify(
            views => views.FrameAsync(harness.DocumentIdOf(scene), It.Is<IReadOnlyList<NodeId>>(ids => ids.SequenceEqual(new[] { node.Id }))),
            Times.Once);
    }

    [TestMethod]
    public async Task ExternalRootSelection_SelectsTheRootRow()
    {
        using var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        harness.SelectionService.Publish(
            harness.DocumentIdOf(scene),
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNodeId: null, PrimaryFolderId: null),
            "Viewport");

        _ = explorer.ShownItems.OfType<SceneAdapter>().Single().IsSelected.Should().BeTrue();
        _ = explorer.SelectedItemsCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ProjectionRebuild_PreservesSelectionByIdentity()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(nodeAdapter!, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);

        // Another view adds a child: the Explorer rebuilds without taking the selection.
        _ = await harness.Insert(scene, SceneNodeTemplates.Empty(scene, "Added"), node.Id).ConfigureAwait(false);

        var rebuiltAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        var added = node.Hierarchy.Children.Should().ContainSingle().Subject;
        _ = rebuiltAdapter.Should().NotBeNull();
        _ = rebuiltAdapter.Should().NotBeSameAs(nodeAdapter, "the rebuild replaces the selected adapter");
        _ = (await explorer.FindAdapterByNodeIdAsync(added.Id).ConfigureAwait(false)).Should().NotBeNull();
        _ = explorer.SelectedItem.Should().BeSameAs(rebuiltAdapter, "the selection comes back on the replacement adapter");
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(scene)).PrimaryNodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public async Task SceneSwitchKeepsPerDocumentSelectionAndRestoresOnReturn()
    {
        using var harness = new SelectionHarness(out var sceneA, out var nodeA, out _);
        var sceneB = harness.AddScene("Scene B");
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(sceneA).ConfigureAwait(false);
        var adapterA = await explorer.FindAdapterByNodeIdAsync(nodeA.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(adapterA!, [.. explorer.ShownItems], isControlDown: false, isShiftDown: false);

        await explorer.HandleDocumentOpenedAsync(sceneB).ConfigureAwait(false);
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(sceneB)).Should().Be(SceneSelectionContext.Empty, "the other document starts unselected");
        _ = explorer.SelectedItemsCount.Should().Be(0, "scene A rows must not stay selected under scene B");

        await explorer.HandleDocumentOpenedAsync(sceneA).ConfigureAwait(false);

        var restored = await explorer.FindAdapterByNodeIdAsync(nodeA.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(restored, "returning to scene A restores its selection by identity");
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(sceneA)).PrimaryNodeId.Should().Be(nodeA.Id);
    }

    [TestMethod]
    public async Task SetSelectedNodes_UnresolvableId_KeepsAuthoritativeContextAndStillNotifies()
    {
        using var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var baseline = harness.Received.Count;
        var unaddressable = NodeId.New();

        var applied = await explorer.SetSelectedNodes([unaddressable]).ConfigureAwait(false);

        _ = applied.Should().BeFalse("the named id has no row in this projection");
        _ = harness.Received.Should().HaveCountGreaterThan(baseline, "a changed authoritative context notifies consumers even when the visible row set does not move");
        var last = harness.Received[^1];
        _ = last.Context.SelectedNodeIds.Should().Equal(unaddressable);
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(scene)).SelectedNodeIds.Should().Equal(unaddressable);
    }

    [TestMethod]
    public async Task SetSelectedNodes_NodeRealizedByLaterRebuild_ReselectsByIdentity()
    {
        using var harness = new SelectionHarness(out var scene, out _, out _);
        var late = SceneNodeTemplates.Empty(scene, "Late");
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        _ = (await explorer.SetSelectedNodes([late.Id]).ConfigureAwait(false)).Should().BeFalse("the node does not exist yet");

        _ = await harness.Insert(scene, late).ConfigureAwait(false);

        var adapter = await explorer.FindAdapterByNodeIdAsync(late.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(adapter, "the rebuild restores the named selection once the row exists");
        _ = (await explorer.SetSelectedNodes([late.Id]).ConfigureAwait(false)).Should().BeTrue();
    }

    [TestMethod]
    public async Task NodeAddedMessage_ReappliesStoredSelectionForTheNewRow()
    {
        using var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var late = SceneNodeTemplates.Empty(scene, "Late");

        // A command reveal stores the node before its adapter exists (PublishNodeAdded order).
        harness.SelectionService.Publish(
            harness.DocumentIdOf(scene),
            new SceneSelectionContext(SceneSelectionKind.Node, [late.Id], [], late.Id, PrimaryFolderId: null),
            "Command");

        _ = await harness.Insert(scene, late).ConfigureAwait(false);
        await Task.Yield();

        var adapter = await explorer.FindAdapterByNodeIdAsync(late.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(adapter, "inserting the adapter resolves the pending selection");
        _ = explorer.ActiveItem.Should().BeSameAs(adapter);
    }

    [TestMethod]
    public async Task NodeAddedMessage_PreservesExistingSelectionInsteadOfSelectingInsertedRow()
    {
        using var harness = new SelectionHarness(out var scene, out var selected, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = await explorer.SetSelectedNodes([selected.Id]).ConfigureAwait(false);
        var added = SceneNodeTemplates.Empty(scene, "Added");

        _ = await harness.Insert(scene, added).ConfigureAwait(false);
        await Task.Yield();

        var context = harness.SelectionService.GetContext(harness.DocumentIdOf(scene));
        _ = context.SelectedNodeIds.Should().Equal(selected.Id);
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindAdapterByNodeIdAsync(selected.Id).ConfigureAwait(false));
        _ = (await explorer.FindAdapterByNodeIdAsync(added.Id).ConfigureAwait(false)).Should().NotBeNull();
    }

    [TestMethod]
    public async Task StoredNestedFolderSelection_RevealsCollapsedAncestors()
    {
        using var harness = new SelectionHarness(out var scene, out _, out var outerId);
        var innerId = FolderId.New();
        var outer = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = outerId,
            Name = "Outer",
            IsExpanded = false,
            Children =
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = innerId,
                    Name = "Inner",
                    IsExpanded = false,
                },
            ],
        };
        harness.TestSceneOf(scene).ReplaceLayout([outer]);
        harness.SelectionService.Publish(
            harness.DocumentIdOf(scene),
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [innerId], PrimaryNodeId: null, innerId),
            "Viewport");
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var selected = await explorer.FindFolderAdapterAsync(innerId).ConfigureAwait(false);
        _ = explorer.ShownItems.Should().Contain(selected!);
        _ = explorer.SelectedItem.Should().BeSameAs(selected);
        _ = selected!.IsSelected.Should().BeTrue();
        _ = outer.IsExpanded.Should().BeFalse("revealing the selected grouping must not author layout expansion");
    }

    [TestMethod]
    public async Task ProjectionRebuild_PreservesSelectionRevealWithoutAuthoringFolderExpansion()
    {
        using var harness = new SelectionHarness(out var scene, out _, out var folderId);
        var entry = harness.TestSceneOf(scene).Layout.Single(value => value.FolderId == folderId);
        entry.IsExpanded = false;
        var nodeId = entry.Children!.Single().NodeId!.Value;
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = (await explorer.SetSelectedNodes([nodeId]).ConfigureAwait(false)).Should().BeTrue();
        var selected = await explorer.FindAdapterByNodeIdAsync(nodeId).ConfigureAwait(false);
        _ = explorer.ShownItems.Should().Contain(selected!);
        _ = explorer.SelectedItem.Should().BeSameAs(selected);
        _ = entry.IsExpanded.Should().BeFalse();
        var original = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);

        // Another view adds a child: the Explorer rebuilds without taking the selection.
        _ = await harness.Insert(scene, SceneNodeTemplates.Empty(scene, "Added"), nodeId).ConfigureAwait(false);

        var replacement = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = replacement.Should().NotBeSameAs(original);
        _ = replacement!.IsExpanded.Should().BeTrue();
        _ = entry.IsExpanded.Should().BeFalse("selection reveal must remain transient after a rebuild");
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindAdapterByNodeIdAsync(nodeId).ConfigureAwait(false));
    }

    [TestMethod]
    public async Task ProjectionRebuild_UsesForeignSelectionWrittenDuringRefill()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out var folderId);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(false);
        var changed = false;
        ((INotifyCollectionChanged)explorer.ShownItems).CollectionChanged += (_, args) =>
        {
            if (!changed && args.Action == NotifyCollectionChangedAction.Reset)
            {
                changed = true;
                harness.SelectionService.Publish(
                    harness.DocumentIdOf(scene),
                    new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId),
                    "Viewport");
            }
        };

        // Another view adds a child: the Explorer rebuilds without taking the selection.
        _ = await harness.Insert(scene, SceneNodeTemplates.Empty(scene, "Added"), node.Id).ConfigureAwait(false);

        _ = changed.Should().BeTrue();
        _ = harness.SelectionService.GetContext(harness.DocumentIdOf(scene)).Kind.Should().Be(SceneSelectionKind.Folder);
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false));
    }

    private sealed partial class SelectionHarness : IDisposable
    {
        private readonly Scene sceneA;
        private readonly List<TestScene> openDocuments = [];

        public SelectionHarness(out Scene scene, out SceneNode freeNode, out FolderId folderId)
        {
            scene = new Scene { Name = "Scene A" };
            this.sceneA = scene;

            var folderNode = new SceneNode(scene, "InFolder");
            freeNode = new SceneNode(scene, "Free");
            scene.RootNodes.Add(folderNode);
            scene.RootNodes.Add(freeNode);

            folderId = FolderId.New();
            IList<ExplorerEntryData> layout =
            [
                new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "Group",
                    IsExpanded = true,
                    Children = [new ExplorerEntryData { Type = "Node", NodeId = folderNode.Id }],
                },
                new ExplorerEntryData { Type = "Node", NodeId = freeNode.Id },
            ];

            var documents = new TestDocumentArea();
            var metadata = TestScenes.Open(scene, layout: layout);
            scene = metadata.Scene;
            freeNode = metadata.NodeOf(freeNode);
            this.sceneA = scene;
            this.openDocuments.Add(metadata);
            documents.Activate(metadata.Document);
            this.Documents = documents;

            this.SelectionService = new SceneSelectionService();
            this.SelectionService.SelectionChanged += (_, args) => this.Received.Add(args);
            _ = this.Views.Setup(views => views.FrameAsync(It.IsAny<DocumentId>(), It.IsAny<IReadOnlyList<NodeId>>())).ReturnsAsync(true);
        }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Editor.Documents.OpenDocuments OpenDocuments => this.openDocuments[0].Documents;
        public SceneSelectionService SelectionService { get; }

        /// <summary>Gets the scene views the Explorer asks to frame nodes.</summary>
        public Mock<ISceneViews> Views { get; } = new();

        /// <summary>Gets every selection change the service announced, whoever wrote it.</summary>
        public List<SceneSelectionChangedEventArgs> Received { get; } = [];

        public void Dispose()
        {
            foreach (var testScene in this.openDocuments)
            {
                testScene.Dispose();
            }
        }

        public Scene AddScene(string name)
        {
            var added = new Scene { Name = name };
            var metadata = TestScenes.Open(added);
            added = metadata.Scene;
            this.openDocuments.Add(metadata);
            this.Documents.Add(metadata.Document);
            return added;
        }

        public SceneExplorerViewModel Build(ICommandService? commands = null)
            => new(
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                this.SelectionService,
                views: this.Views.Object,
                commands: commands);

        /// <summary>Appends a node to a scene of the harness through its document, as the editor's create actions do.</summary>
        /// <param name="scene">The scene.</param>
        /// <param name="node">The node's data.</param>
        /// <param name="parent">The node's parent, or <see langword="null"/> for a root node.</param>
        /// <returns>The edit's result.</returns>
        public Task<SceneEditResult> Insert(Scene scene, SceneNodeData node, NodeId? parent = null)
        {
            var testScene = this.TestSceneOf(scene);
            return testScene.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(scene, testScene.Layout, node, parent)]));
        }

        /// <summary>Gets the identity of the document that edits a scene of the harness.</summary>
        /// <param name="scene">The scene.</param>
        /// <returns>The document's identity.</returns>
        public DocumentId DocumentIdOf(Scene scene) => this.TestSceneOf(scene).DocumentId;

        /// <summary>Gets the document that edits a scene of the harness.</summary>
        /// <param name="scene">The scene.</param>
        /// <returns>The opened scene of the document.</returns>
        public TestScene TestSceneOf(Scene scene) => this.openDocuments.Single(testScene => ReferenceEquals(testScene.Scene, scene));
    }
}
