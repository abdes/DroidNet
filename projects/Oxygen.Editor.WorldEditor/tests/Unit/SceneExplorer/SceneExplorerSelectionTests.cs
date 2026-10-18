// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>
/// C16-C19/T5: the Explorer publishes the full classified selection (row kinds, ordered
/// identities, explicit primary), reconciles foreign selections by identity, restores the
/// selection across projection rebuilds and scene switches, and keeps the root row exclusive.
/// </summary>
[TestClass]
public sealed partial class SceneExplorerSelectionTests
{
    [TestMethod]
    public async Task FolderOnlySelection_PublishesFolderKindAndTypedMessageContext()
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
        var message = harness.Received[^1];
        _ = message.SelectionContext.Kind.Should().Be(SceneSelectionKind.Folder);
        _ = message.SelectedEntities.Should().BeEmpty();
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
        var requests = new List<FrameSceneNodesRequestMessage>();
        harness.Messenger.Register<FrameSceneNodesRequestMessage>(requests, (recipient, message) => ((List<FrameSceneNodesRequestMessage>)recipient).Add(message));
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        explorer.InvokeItem(nodeAdapter!);

        _ = requests.Should().ContainSingle();
        _ = requests[0].DocumentId.Should().Be(harness.DocumentIdOf(scene));
        _ = requests[0].NodeIds.Should().Equal(node.Id);
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
        var added = node.Children.Should().ContainSingle().Subject;
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
    public async Task MessageWithoutContext_DerivesNodeClassificationFromEntities()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var adapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        var message = new SceneNodeSelectionChangedMessage([node]);

        _ = message.SelectionContext.Kind.Should().Be(SceneSelectionKind.Node);
        _ = message.SelectionContext.PrimaryNodeId.Should().Be(node.Id);
        _ = new SceneNodeSelectionChangedMessage([]).SelectionContext.Should().Be(SceneSelectionContext.Empty, "an empty node-only publish is Empty, not Scene");
        _ = adapter.Should().NotBeNull();
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
        _ = harness.Received.Should().HaveCount(baseline + 1, "a changed authoritative context notifies consumers even when the visible row set does not move");
        var last = harness.Received[^1];
        _ = last.SelectionContext.SelectedNodeIds.Should().Equal(unaddressable);
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
        harness.TabOf(scene).ReplaceLayout([outer]);
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
        var entry = harness.TabOf(scene).Layout.Single(value => value.FolderId == folderId);
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
        private readonly List<SceneTab> openDocuments = [];

        public SelectionHarness(out Scene scene, out SceneNode freeNode, out FolderId folderId)
        {
            var project = new Project(new ProjectInfo("Selection", Category.Games, "H:/SelectionTests", "preview.png"));
            scene = new Scene { Name = "Scene A" };
            this.sceneA = scene;

            var folderNode = new SceneNode(scene) { Name = "InFolder" };
            freeNode = new SceneNode(scene) { Name = "Free" };
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

            var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
            _ = manager.SetupGet(value => value.CurrentProject).Returns(project);
            this.Manager = manager;

            var documents = new TestDocumentArea();
            var metadata = SceneTabs.Open(scene, layout: layout);
            scene = metadata.Scene;
            freeNode = metadata.NodeOf(freeNode);
            this.sceneA = scene;
            this.openDocuments.Add(metadata);
            documents.Activate(metadata.Document);
            this.Documents = documents;

            this.SelectionService = new SceneSelectionService();
            this.Messenger = new StrongReferenceMessenger();
            this.Messenger.Register<SceneNodeSelectionChangedMessage>(this, (recipient, message) => ((SelectionHarness)recipient).Received.Add(message));
        }

        public Mock<IProjectManagerService> Manager { get; }

        public TestDocumentArea Documents { get; }

        /// <summary>Gets the open documents that hold the harness's first scene.</summary>
        public Editor.Documents.OpenDocuments OpenDocuments => this.openDocuments[0].Documents;
        public SceneSelectionService SelectionService { get; }

        public StrongReferenceMessenger Messenger { get; }

        public List<SceneNodeSelectionChangedMessage> Received { get; } = [];

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

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                this.Messenger,
                Mock.Of<IRouter>(),
                this.Documents,
                this.OpenDocuments,
                default,
                this.SelectionService);

        /// <summary>Appends a node to a scene of the harness through its document, as the editor's create actions do.</summary>
        /// <param name="scene">The scene.</param>
        /// <param name="node">The node's data.</param>
        /// <param name="parent">The node's parent, or <see langword="null"/> for a root node.</param>
        /// <returns>The edit's result.</returns>
        public Task<SceneEditResult> Insert(Scene scene, SceneNodeData node, NodeId? parent = null)
        {
            var tab = this.TabOf(scene);
            return tab.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(scene, tab.Layout, node, parent)]));
        }

        /// <summary>Gets the identity of the document that edits a scene of the harness.</summary>
        /// <param name="scene">The scene.</param>
        /// <returns>The document's identity.</returns>
        public DocumentId DocumentIdOf(Scene scene) => this.TabOf(scene).DocumentId;

        /// <summary>Gets the document that edits a scene of the harness.</summary>
        /// <param name="scene">The scene.</param>
        /// <returns>The document's tab.</returns>
        public SceneTab TabOf(Scene scene) => this.openDocuments.Single(tab => ReferenceEquals(tab.Scene, scene));
    }
}
