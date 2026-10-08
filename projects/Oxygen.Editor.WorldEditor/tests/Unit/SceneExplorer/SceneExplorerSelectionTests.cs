// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
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
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>
/// C16-C19/T5: the Explorer publishes the full classified selection (row kinds, ordered
/// identities, explicit primary), reconciles foreign selections by identity, restores the
/// selection across projection rebuilds and scene switches, and keeps the root row exclusive.
/// </summary>
[TestClass]
public sealed class SceneExplorerSelectionTests
{
    [TestMethod]
    public async Task FolderOnlySelection_PublishesFolderKindAndTypedMessageContext()
    {
        var harness = new SelectionHarness(out var scene, out _, out var folderId);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        explorer.SelectDisplayedItem(folder!, explorer.ShownItems.ToList(), isControlDown: false, isShiftDown: false);

        var context = harness.SelectionService.GetContext(scene.Id);
        _ = context.Kind.Should().Be(SceneSelectionKind.Folder);
        _ = context.SelectedNodeIds.Should().BeEmpty("a folder selection must not claim its contained nodes");
        _ = context.SelectedFolderIds.Should().Equal(folderId);
        _ = context.PrimaryFolderId.Should().Be(folderId);
        var message = harness.Received.Last();
        _ = message.SelectionContext.Kind.Should().Be(SceneSelectionKind.Folder);
        _ = message.SelectedEntities.Should().BeEmpty();
    }

    [TestMethod]
    public async Task NodeAndFolderBatch_PublishesMixedKindWithBothIdentities()
    {
        var harness = new SelectionHarness(out var scene, out var freeNode, out var folderId);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var folder = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(freeNode.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(folder!, explorer.ShownItems.ToList(), isControlDown: false, isShiftDown: false);
        explorer.SelectDisplayedItem(nodeAdapter!, explorer.ShownItems.ToList(), isControlDown: true, isShiftDown: false);

        var context = harness.SelectionService.GetContext(scene.Id);
        _ = context.Kind.Should().Be(SceneSelectionKind.Mixed);
        _ = context.SelectedNodeIds.Should().Equal(freeNode.Id);
        _ = context.SelectedFolderIds.Should().Equal(folderId);
        _ = context.PrimaryNodeId.Should().Be(freeNode.Id, "the explicitly active row is the primary, not the last in row order");
    }

    [TestMethod]
    public async Task RootRowSelection_IsExclusiveAndClassifiesScene()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(nodeAdapter!, explorer.ShownItems.ToList(), isControlDown: false, isShiftDown: false);

        var root = explorer.ShownItems.OfType<SceneAdapter>().Single();
        explorer.SelectDisplayedItem(root, explorer.ShownItems.ToList(), isControlDown: true, isShiftDown: false);

        _ = explorer.SelectedItemsCount.Should().Be(1, "the root row never shares a selection");
        _ = harness.SelectionService.GetContext(scene.Id).Kind.Should().Be(SceneSelectionKind.Scene);

        // And the reverse direction: with the root selected, ctrl-clicking a node replaces it.
        explorer.SelectDisplayedItem(nodeAdapter!, explorer.ShownItems.ToList(), isControlDown: true, isShiftDown: false);

        _ = explorer.SelectedItemsCount.Should().Be(1);
        _ = harness.SelectionService.GetContext(scene.Id).Kind.Should().Be(SceneSelectionKind.Node);
    }

    [TestMethod]
    public async Task ExternalSelectionByViewport_ReselectsExplorerRowsAndRepublishes()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        harness.SelectionService.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Node, [node.Id], [], node.Id, null),
            "Viewport");

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(nodeAdapter, "foreign selections reach the rows by identity");
        _ = explorer.ActiveItem.Should().BeSameAs(nodeAdapter);
        var context = harness.SelectionService.GetContext(scene.Id);
        _ = context.Kind.Should().Be(SceneSelectionKind.Node);
        _ = context.PrimaryNodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public async Task ViewportSelection_ScrollsTheActiveRowIntoView()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var revealed = new List<ITreeItem>();
        explorer.BringIntoViewRequested += (_, args) => revealed.Add(args.TreeItem);

        harness.SelectionService.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Node, [node.Id], [], node.Id, null),
            "Viewport");

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = revealed.Should().Equal(nodeAdapter!);
    }

    [TestMethod]
    public async Task InvokingANodeRow_AsksToFrameTheNode()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var requests = new List<FrameSceneNodesRequestMessage>();
        harness.Messenger.Register<FrameSceneNodesRequestMessage>(requests, (recipient, message) => ((List<FrameSceneNodesRequestMessage>)recipient).Add(message));
        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);

        explorer.InvokeItem(nodeAdapter!);

        _ = requests.Should().ContainSingle();
        _ = requests[0].DocumentId.Should().Be(scene.Id);
        _ = requests[0].NodeIds.Should().Equal(node.Id);
    }

    [TestMethod]
    public async Task ExternalRootSelection_SelectsTheRootRow()
    {
        var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        harness.SelectionService.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], null, null),
            "Viewport");

        _ = explorer.ShownItems.OfType<SceneAdapter>().Single().IsSelected.Should().BeTrue();
        _ = explorer.SelectedItemsCount.Should().Be(1);
    }

    [TestMethod]
    public async Task ProjectionRebuild_PreservesSelectionByIdentity()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
        var added = new SceneNode(scene) { Name = "Added" };
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), node.Id, null, It.IsAny<string>()))
            .Callback(() => node.AddChild(added))
            .ReturnsAsync(SceneCommandResults.Success(added));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        var nodeAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(nodeAdapter!, explorer.ShownItems.ToList(), isControlDown: false, isShiftDown: false);

        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var rebuiltAdapter = await explorer.FindAdapterByNodeIdAsync(node.Id).ConfigureAwait(false);
        _ = rebuiltAdapter.Should().NotBeNull();
        _ = rebuiltAdapter.Should().NotBeSameAs(nodeAdapter, "the rebuild replaces the selected adapter");
        _ = (await explorer.FindAdapterByNodeIdAsync(added.Id).ConfigureAwait(false)).Should().NotBeNull();
        _ = added.Parent.Should().BeSameAs(node);
        _ = explorer.SelectedItem.Should().BeSameAs(rebuiltAdapter, "the selection comes back on the replacement adapter");
        _ = harness.SelectionService.GetContext(scene.Id).PrimaryNodeId.Should().Be(node.Id);
    }

    [TestMethod]
    public async Task SceneSwitchKeepsPerDocumentSelectionAndRestoresOnReturn()
    {
        var harness = new SelectionHarness(out var sceneA, out var nodeA, out _);
        var sceneB = harness.AddScene("Scene B");
        using var explorer = harness.Build();

        await explorer.HandleDocumentOpenedAsync(sceneA).ConfigureAwait(false);
        var adapterA = await explorer.FindAdapterByNodeIdAsync(nodeA.Id).ConfigureAwait(false);
        explorer.SelectDisplayedItem(adapterA!, explorer.ShownItems.ToList(), isControlDown: false, isShiftDown: false);

        await explorer.HandleDocumentOpenedAsync(sceneB).ConfigureAwait(false);
        _ = harness.SelectionService.GetContext(sceneB.Id).Should().Be(SceneSelectionContext.Empty, "the other document starts unselected");
        _ = explorer.SelectedItemsCount.Should().Be(0, "scene A rows must not stay selected under scene B");

        await explorer.HandleDocumentOpenedAsync(sceneA).ConfigureAwait(false);

        var restored = await explorer.FindAdapterByNodeIdAsync(nodeA.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(restored, "returning to scene A restores its selection by identity");
        _ = harness.SelectionService.GetContext(sceneA.Id).PrimaryNodeId.Should().Be(nodeA.Id);
    }

    [TestMethod]
    public async Task MessageWithoutContext_DerivesNodeClassificationFromEntities()
    {
        var harness = new SelectionHarness(out var scene, out var node, out _);
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
        var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var baseline = harness.Received.Count;
        var unaddressable = Guid.NewGuid();

        var applied = await explorer.SetSelectedNodes([unaddressable]).ConfigureAwait(false);

        _ = applied.Should().BeFalse("the named id has no row in this projection");
        _ = harness.Received.Count.Should().Be(baseline + 1, "a changed authoritative context notifies consumers even when the visible row set does not move");
        var last = harness.Received[^1];
        _ = last.SelectionContext.SelectedNodeIds.Should().Equal(unaddressable);
        _ = harness.SelectionService.GetContext(scene.Id).SelectedNodeIds.Should().Equal(unaddressable);
    }

    [TestMethod]
    public async Task SetSelectedNodes_NodeRealizedByLaterRebuild_ReselectsByIdentity()
    {
        var harness = new SelectionHarness(out var scene, out _, out _);
        var late = new SceneNode(scene) { Name = "Late" };
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), null, null, It.IsAny<string>()))
            .Callback(() => scene.RootNodes.Add(late))
            .ReturnsAsync(SceneCommandResults.Success(late));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        _ = (await explorer.SetSelectedNodes([late.Id]).ConfigureAwait(false)).Should().BeFalse("the node does not exist yet");

        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var adapter = await explorer.FindAdapterByNodeIdAsync(late.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(adapter, "the rebuild restores the named selection once the row exists");
        _ = (await explorer.SetSelectedNodes([late.Id]).ConfigureAwait(false)).Should().BeTrue();
    }

    [TestMethod]
    public async Task NodeAddedMessage_ReappliesStoredSelectionForTheNewRow()
    {
        var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        var late = new SceneNode(scene) { Name = "Late" };
        scene.RootNodes.Add(late);

        // A command reveal stores the node before its adapter exists (PublishNodeAdded order).
        harness.SelectionService.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Node, [late.Id], [], late.Id, null),
            "Command");

        harness.Messenger.Send(new SceneNodeAddedMessage([late]));
        await Task.Yield();

        var adapter = await explorer.FindAdapterByNodeIdAsync(late.Id).ConfigureAwait(false);
        _ = explorer.SelectedItem.Should().BeSameAs(adapter, "inserting the adapter resolves the pending selection");
        _ = explorer.ActiveItem.Should().BeSameAs(adapter);
    }

    [TestMethod]
    public async Task NodeAddedMessage_PreservesExistingSelectionInsteadOfSelectingInsertedRow()
    {
        var harness = new SelectionHarness(out var scene, out var selected, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = await explorer.SetSelectedNodes([selected.Id]).ConfigureAwait(false);
        var added = new SceneNode(scene) { Name = "Added" };
        scene.RootNodes.Add(added);

        harness.Messenger.Send(new SceneNodeAddedMessage([added]));
        await Task.Yield();

        var context = harness.SelectionService.GetContext(scene.Id);
        _ = context.SelectedNodeIds.Should().Equal(selected.Id);
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindAdapterByNodeIdAsync(selected.Id).ConfigureAwait(false));
        _ = (await explorer.FindAdapterByNodeIdAsync(added.Id).ConfigureAwait(false)).Should().NotBeNull();
    }

    [TestMethod]
    public async Task StoredNestedFolderSelection_RevealsCollapsedAncestors()
    {
        var harness = new SelectionHarness(out var scene, out _, out var outerId);
        var innerId = Guid.NewGuid();
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
        scene.SetExplorerLayout([outer]);
        harness.SelectionService.Publish(
            scene.Id,
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [innerId], null, innerId),
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
        var harness = new SelectionHarness(out var scene, out _, out var folderId);
        var entry = scene.ExplorerLayout!.Single(value => value.FolderId == folderId);
        entry.IsExpanded = false;
        var nodeId = entry.Children!.Single().NodeId!.Value;
        var parent = scene.AllNodes.Single(node => node.Id == nodeId);
        var added = new SceneNode(scene) { Name = "Added" };
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), nodeId, null, It.IsAny<string>()))
            .Callback(() => parent.AddChild(added))
            .ReturnsAsync(SceneCommandResults.Success(added));
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = (await explorer.SetSelectedNodes([nodeId]).ConfigureAwait(false)).Should().BeTrue();
        var selected = await explorer.FindAdapterByNodeIdAsync(nodeId).ConfigureAwait(false);
        _ = explorer.ShownItems.Should().Contain(selected!);
        _ = explorer.SelectedItem.Should().BeSameAs(selected);
        _ = entry.IsExpanded.Should().BeFalse();
        var original = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);

        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        var replacement = await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false);
        _ = replacement.Should().NotBeSameAs(original);
        _ = replacement!.IsExpanded.Should().BeTrue();
        _ = entry.IsExpanded.Should().BeFalse("selection reveal must remain transient after a rebuild");
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindAdapterByNodeIdAsync(nodeId).ConfigureAwait(false));
    }

    [TestMethod]
    public async Task ProjectionRebuild_UsesForeignSelectionWrittenDuringRefill()
    {
        var harness = new SelectionHarness(out var scene, out var node, out var folderId);
        var added = new SceneNode(scene) { Name = "Added" };
        _ = harness.Commands
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneDocumentCommandContext>(), node.Id, null, It.IsAny<string>()))
            .Callback(() => node.AddChild(added))
            .ReturnsAsync(SceneCommandResults.Success(added));
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
                    scene.Id,
                    new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], null, folderId),
                    "Viewport");
            }
        };

        await explorer.AddEntityCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = changed.Should().BeTrue();
        _ = harness.SelectionService.GetContext(scene.Id).Kind.Should().Be(SceneSelectionKind.Folder);
        _ = explorer.SelectedItem.Should().BeSameAs(await explorer.FindFolderAdapterAsync(folderId).ConfigureAwait(false));
    }

    private sealed class SelectionHarness
    {
        private readonly Scene sceneA;
        private readonly List<SceneDocumentMetadata> openDocuments = [];

        public SelectionHarness(out Scene scene, out SceneNode freeNode, out Guid folderId)
        {
            var project = new Project(new ProjectInfo("Selection", Category.Games, "H:/SelectionTests", "preview.png")) { Name = "Selection" };
            scene = new Scene(project) { Name = "Scene A" };
            this.sceneA = scene;

            var folderNode = new SceneNode(scene) { Name = "InFolder" };
            freeNode = new SceneNode(scene) { Name = "Free" };
            scene.RootNodes.Add(folderNode);
            scene.RootNodes.Add(freeNode);
            project.Scenes.Add(scene);

            folderId = Guid.NewGuid();
            scene.SetExplorerLayout(
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
                ]);

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
            this.SelectionService = new SceneSelectionService();
            this.Messenger = new StrongReferenceMessenger();
            this.Messenger.Register<SceneNodeSelectionChangedMessage>(this, (recipient, message) => ((SelectionHarness)recipient).Received.Add(message));
        }

        public Mock<IProjectManagerService> Manager { get; }

        public Mock<IDocumentService> Documents { get; }

        public Mock<ISceneEngineSync> Sync { get; }

        public Mock<ISceneDocumentCommandService> Commands { get; }

        public SceneSelectionService SelectionService { get; }

        public StrongReferenceMessenger Messenger { get; }

        public List<SceneNodeSelectionChangedMessage> Received { get; } = [];

        public Scene AddScene(string name)
        {
            var added = new Scene(this.sceneA.Project) { Name = name };
            this.sceneA.Project.Scenes.Add(added);
            var metadata = new SceneDocumentMetadata(added.Id);
            this.openDocuments.Add(metadata);
            _ = this.Sync.Setup(value => value.GetDocumentScene(metadata)).Returns(added);
            _ = this.Sync.Setup(value => value.RegisterDocument(added, metadata)).Returns(value: true);
            return added;
        }

        public SceneExplorerViewModel Build()
            => new(
                this.Manager.Object,
                this.Messenger,
                Mock.Of<IRouter>(),
                this.Documents.Object,
                default,
                this.Sync.Object,
                this.SelectionService,
                this.Commands.Object);
    }
}
