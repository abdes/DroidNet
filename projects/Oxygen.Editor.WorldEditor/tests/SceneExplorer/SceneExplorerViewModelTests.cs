// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure;
using static Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure.SceneExplorerViewModelTestFixture;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneExplorerViewModelTests
{
    [TestMethod]
    public void OnItemBeingAdded_RoutesToMutatorForSceneNodeParent()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var parentNode = new SceneNode(scene) { Name = "Parent" };
        scene.RootNodes.Add(parentNode);
        var parentAdapter = new LayoutNodeAdapter(parentNode);

        var newNode = new LayoutNodeAdapter(new SceneNode(scene) { Name = "Child" });

        var args = new TreeItemBeingAddedEventArgs { Parent = parentAdapter, TreeItem = newNode };

        vm.InvokeHandleItemBeingAdded(scene, newNode, parentAdapter, args);

        mutator.Verify(m => m.CreateNodeUnderParent(newNode.AttachedObject, parentNode, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_RoutesToOrganizerForFolderParent()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder");

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);

        var args = new TreeItemBeingAddedEventArgs { Parent = folder, TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, folder, args);

        organizer.Verify(o => o.MoveNodeToFolder(node.Id, folder.Id, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_WithSceneAdapterParentCreatesNodeAtRoot()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var nodeAdapter = new LayoutNodeAdapter(new SceneNode(scene) { Name = "NewNode" });
        var args = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, sceneAdapter, args);

        mutator.Verify(m => m.CreateNodeAtRoot(nodeAdapter.AttachedObject, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_NoSelectionReturnsEmpty()
    {
        var (vm, _, _, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Single;

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEmpty();
        _ = capture.UsedShownItemsFallback.Should().BeFalse();
    }

    [TestMethod]
    public void OnItemBeingAdded_ReparentsExistingNodeBetweenParents()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var parentA = new SceneNode(scene) { Name = "ParentA" };
        var parentB = new SceneNode(scene) { Name = "ParentB" };
        var child = new SceneNode(scene) { Name = "Child" };
        parentA.AddChild(child);
        scene.RootNodes.Add(parentA);
        scene.RootNodes.Add(parentB);

        var parentAAdapter = new LayoutNodeAdapter(parentA);
        var parentBAdapter = new LayoutNodeAdapter(parentB);
        var childAdapter = new LayoutNodeAdapter(child);

        var args = new TreeItemBeingAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter };

        vm.InvokeHandleItemBeingAdded(scene, childAdapter, parentBAdapter, args);

        mutator.Verify(m => m.ReparentNode(child.Id, parentA.Id, parentB.Id, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingRemoved_WhenDeleteCallsMutatorRemove()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);

        var args = new TreeItemBeingRemovedEventArgs { TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingRemoved(scene, nodeAdapter, args);

        mutator.Verify(m => m.RemoveNode(node.Id, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingRemoved_DuringMoveCapturesOldParentForReparent()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var parentAdapter = new LayoutNodeAdapter(parent);
        var childAdapter = new LayoutNodeAdapter(child);

        // Capture move intent without reflection
        vm.CaptureOldParentForMove(childAdapter, parentAdapter);
        child.SetParent(newParent: null);

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var addArgs = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = childAdapter };

        vm.InvokeHandleItemBeingAdded(scene, childAdapter, sceneAdapter, addArgs);

        mutator.Verify(m => m.RemoveNode(It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
        mutator.Verify(m => m.ReparentNode(child.Id, parent.Id, null, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_WithCapturedOldParentReparentsToRoot()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var childAdapter = new LayoutNodeAdapter(child);

        vm.CaptureOldParentForMove(childAdapter, new LayoutNodeAdapter(parent));
        child.SetParent(newParent: null);

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var addArgs = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = childAdapter };

        vm.InvokeHandleItemBeingAdded(scene, childAdapter, sceneAdapter, addArgs);

        mutator.Verify(m => m.ReparentNode(child.Id, parent.Id, null, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeAtRoot(It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_WhenFolderMoveRejectedSetsProceedFalse()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder");

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);

        var layout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };
        scene.ExplorerLayout = layout;

        var adapter = new LayoutNodeAdapter(node);
        var args = new TreeItemBeingAddedEventArgs { Parent = folder, TreeItem = adapter, Proceed = true };

        organizer.Setup(o => o.MoveNodeToFolder(node.Id, folder.Id, scene))
            .Throws(new InvalidOperationException("Cannot move node to a folder outside its lineage."));

        vm.InvokeHandleItemBeingAdded(scene, adapter, folder, args);

        _ = args.Proceed.Should().BeFalse();
        _ = scene.ExplorerLayout.Should().BeSameAs(layout);

        organizer.Verify(o => o.MoveNodeToFolder(node.Id, folder.Id, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeAtRoot(It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public async Task ExpandAndSelectFolderAsync_ExpandsExistingFolder()
    {
        var (vm, scene, _, _, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);

        var folderId = Guid.NewGuid();
        var folderAdapter = new FolderAdapter(folderId, "Folder");
        await vm.AddFolderToSceneAdapter(sceneAdapter, folderAdapter).ConfigureAwait(false);

        var folderEntry = new ExplorerEntryData { FolderId = folderId, Name = "Folder" };

        vm.SelectionMode = SelectionMode.Single;
        await vm.InvokeExpandAndSelectFolderAsync(folderAdapter, folderEntry).ConfigureAwait(false);

        _ = folderAdapter.IsExpanded.Should().BeTrue();
        _ = folderAdapter.IsSelected.Should().BeTrue();
    }

    [TestMethod]
    public async Task RemoveSelectedItems_DeletesSelectedNode()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);
        var nodeAdapter = sceneAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult().OfType<LayoutNodeAdapter>().Single();

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(nodeAdapter);

        await vm.RemoveSelectedItemsForTestAsync().ConfigureAwait(false);

        mutator.Verify(m => m.RemoveNode(node.Id, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_UsesSingleSelectionModel()
    {
        var (vm, scene, _, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Single;

        var adapter = new LayoutNodeAdapter(new SceneNode(scene) { Name = "Node" });
        vm.ShownItems.Add(adapter);
        vm.SelectItem(adapter);

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEquivalentTo(new[] { adapter.AttachedObject.Id });
        _ = capture.UsedShownItemsFallback.Should().BeFalse();
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_UsesMultipleSelectionModelAndIgnoresFolders()
    {
        var (vm, scene, _, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Multiple;

        var nodeAdapter = new LayoutNodeAdapter(new SceneNode(scene) { Name = "Node" });
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder");
        vm.ShownItems.Add(folder);
        vm.ShownItems.Add(nodeAdapter);

        vm.SelectItem(folder);
        vm.SelectItem(nodeAdapter);

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEquivalentTo(new[] { nodeAdapter.AttachedObject.Id });
        _ = capture.UsedShownItemsFallback.Should().BeFalse();
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_FallsBackToShownItems()
    {
        var (vm, scene, _, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.None;

        var shownAdapter = new LayoutNodeAdapter(new SceneNode(scene) { Name = "Shown" }) { IsSelected = true };
        vm.ShownItems.Add(shownAdapter);

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEquivalentTo(new[] { shownAdapter.AttachedObject.Id });
        _ = capture.UsedShownItemsFallback.Should().BeTrue();
    }

    [TestMethod]
    public void CreateFolderCreationContext_WhenFolderMissingIdReturnsNull()
    {
        var (vm, scene, _, organizer, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var layout = new List<ExplorerEntryData>();
        var folderEntry = new ExplorerEntryData { Name = "Folder" }; // no FolderId
        organizer.Setup(o => o.CreateFolderFromSelection(It.IsAny<HashSet<Guid>>(), scene, sceneAdapter))
            .Returns(new LayoutChangeRecord("CreateFolder", null, layout, folderEntry));

        var result = vm.CreateFolderCreationContext(sceneAdapter, scene, new HashSet<Guid> { Guid.NewGuid() });

        _ = result.Should().BeNull();
        _ = scene.ExplorerLayout.Should().BeNull();
    }

    [TestMethod]
    public void CreateFolderCreationContext_AppliesLayoutAndBuildsFolderAdapter()
    {
        var (vm, scene, _, organizer, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var layout = new List<ExplorerEntryData>();
        var folderId = Guid.NewGuid();
        var folderEntry = new ExplorerEntryData { FolderId = folderId, Name = "New Folder" };
        organizer.Setup(o => o.CreateFolderFromSelection(It.IsAny<HashSet<Guid>>(), scene, sceneAdapter))
            .Returns(new LayoutChangeRecord("CreateFolder", null, layout, folderEntry));

        var result = vm.CreateFolderCreationContext(sceneAdapter, scene, new HashSet<Guid> { Guid.NewGuid() });

        _ = result.Should().NotBeNull();
        _ = scene.ExplorerLayout.Should().BeSameAs(layout);
        _ = result!.Value.FolderAdapter.Id.Should().Be(folderId);
        _ = result.Value.FolderAdapter.Label.Should().Be("New Folder");
    }


    [TestMethod]
    public async Task OnItemBeingRemoved_FromFolder_UpdatesLayoutOnly()
    {
        var (vm, scene, mutator, organizer, _, _) = CreateViewModel();
        var folderId = Guid.NewGuid();
        var folderAdapter = new FolderAdapter(folderId, "Folder");

        // We need to add folder to scene first so it's part of the tree?
        // Or just use it as a parent.
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, folderAdapter).ConfigureAwait(false);

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);

        // Insert node into folder
        await vm.InvokeInsertItemAsync(0, folderAdapter, nodeAdapter).ConfigureAwait(false);

        // Verify parentage
        _ = nodeAdapter.Parent.Should().Be(folderAdapter);

        // Trigger removal
        var args = new TreeItemBeingRemovedEventArgs { TreeItem = nodeAdapter };
        vm.InvokeOnItemBeingRemoved(args);

        // Verify organizer called
        organizer.Verify(o => o.RemoveNodeFromFolder(node.Id, folderId, scene), Times.Once);
        // Verify mutator NOT called (since we are not deleting)
        mutator.Verify(m => m.RemoveNode(It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public async Task Undo_CreateNode_TriggersMutatorRemove()
    {
        var (vm, scene, mutator, _, _, _) = CreateViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);

        // Create a node
        vm.SelectionMode = SelectionMode.Single;
        await vm.AddEntityCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify creation happened
        mutator.Verify(m => m.CreateNodeAtRoot(It.IsAny<SceneNode>(), scene), Times.Once);
        mutator.Invocations.Clear(); // Reset verification

        // Undo
        UndoRedo.Default[vm].Undo();

        // Verify removal happened via mutator (because isPerformingDelete should be true)
        mutator.Verify(m => m.RemoveNode(It.IsAny<Guid>(), scene), Times.Once);
    }

    [TestMethod]
    public async Task RemoveSelectedItems_BatchesEngineCalls()
    {
        var (vm, scene, _, _, messenger, engineSync) = CreateViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        // Setup: Root -> Node1, Node2
        var node1 = new SceneNode(scene) { Name = "Node1" };
        var node2 = new SceneNode(scene) { Name = "Node2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Add to tree
        var node1Adapter = new LayoutNodeAdapter(node1);
        var node2Adapter = new LayoutNodeAdapter(node2);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, node1Adapter).ConfigureAwait(false);
        await vm.InvokeInsertItemAsync(1, sceneAdapter, node2Adapter).ConfigureAwait(false);

        // Select both
        vm.SelectionMode = SelectionMode.Multiple;
        vm.SelectItem(node1Adapter);
        vm.SelectItem(node2Adapter);

        // Capture messages
        var messages = new List<SceneNodeRemovedMessage>();
        messenger.Register<SceneNodeRemovedMessage>(this, (r, m) => messages.Add(m));

        // Remove
        await vm.RemoveSelectedItemsCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify batch call
        engineSync.Verify(es => es.RemoveNodeHierarchiesAsync(It.Is<IReadOnlyList<Guid>>(l => l.Count == 2)), Times.Once);
        engineSync.Verify(es => es.RemoveNodeAsync(It.IsAny<Guid>()), Times.Never);

        // Verify messaging
        messages.Should().HaveCount(1);
        messages[0].Nodes.Should().HaveCount(2);
        messages[0].Nodes.Should().Contain(node1);
        messages[0].Nodes.Should().Contain(node2);
    }

    [TestMethod]
    public async Task CreateFolderFromSelection_DoesNotSyncEngine()
    {
        var (vm, scene, _, _, _, engineSync) = CreateViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, nodeAdapter).ConfigureAwait(false);

        engineSync.Invocations.Clear();

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(nodeAdapter);

        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify no engine sync calls
        engineSync.Verify(es => es.SyncSceneAsync(It.IsAny<Scene>()), Times.Never); // Called during load but cleared
        engineSync.Verify(es => es.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>()), Times.Never);
        engineSync.Verify(es => es.ReparentNodeAsync(It.IsAny<Guid>(), It.IsAny<Guid?>()), Times.Never);
        engineSync.Verify(es => es.RemoveNodeAsync(It.IsAny<Guid>()), Times.Never);
        engineSync.Verify(es => es.RemoveNodeHierarchiesAsync(It.IsAny<IReadOnlyList<Guid>>()), Times.Never);
        engineSync.Verify(es => es.ReparentHierarchiesAsync(It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<Guid?>()), Times.Never);
    }

    [TestMethod]
    public async Task RegisterCreateFolderUndo_UndoRedoRestoresFolderLayout()
    {
        var (vm, scene, _, organizer, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);

        var previousLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };
        scene.ExplorerLayout = previousLayout;

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);

        var folderId = Guid.NewGuid();
        var folderEntry = new ExplorerEntryData
        {
            Type = "Folder",
            FolderId = folderId,
            Name = "Folder",
            Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } },
        };
        var newLayout = new List<ExplorerEntryData> { folderEntry };
        scene.ExplorerLayout = newLayout;

        var layoutContext = new TestLayoutContext();
        await organizer.ReconcileLayoutAsync(sceneAdapter, scene, newLayout, layoutContext, preserveNodeExpansion: true).ConfigureAwait(false);

        var change = new LayoutChangeRecord("CreateFolder", previousLayout, newLayout, folderEntry);

        vm.InvokeRegisterCreateFolderUndo(sceneAdapter, scene, change, "Folder");

        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(previousLayout, options => options.WithStrictOrdering());

        UndoRedo.Default[vm].Redo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(newLayout, options => options.WithStrictOrdering());

        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterRedo = childrenAfterRedo.OfType<FolderAdapter>().Single();
        var folderKidsAfterRedo = await folderAfterRedo.Children.ConfigureAwait(false);
        _ = folderKidsAfterRedo.OfType<LayoutNodeAdapter>().Should().ContainSingle(c => ReferenceEquals(c.AttachedObject, node));
    }

    private sealed class TestLayoutContext : ILayoutContext
    {
        public int RefreshCount { get; private set; }

        public int? GetShownIndex(ITreeItem item) => null;

        public bool TryRemoveShownItem(ITreeItem item)
        {
            _ = item;
            return false;
        }

        public void InsertShownItem(int index, ITreeItem item)
        {
            _ = index;
            _ = item;
        }

        public Task RefreshTreeAsync(SceneAdapter sceneAdapter)
        {
            _ = sceneAdapter;
            ++this.RefreshCount;
            return Task.CompletedTask;
        }

        public bool TryGetVisibleSpan(ITreeItem root, out int startIndex, out int count)
        {
            _ = root;
            startIndex = 0;
            count = 0;
            return false;
        }

        public void ApplyShownDelta(ShownItemsDelta delta)
        {
            _ = delta;
        }
    }
}
