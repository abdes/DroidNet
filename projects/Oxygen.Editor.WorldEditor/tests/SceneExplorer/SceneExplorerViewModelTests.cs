// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.TimeMachine;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
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
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

        var parentNode = new SceneNode(scene) { Name = "Parent" };
        scene.RootNodes.Add(parentNode);
        var parentAdapter = new SceneNodeAdapter(parentNode);

        var newNode = new SceneNodeAdapter(new SceneNode(scene) { Name = "Child" });

        var args = new TreeItemBeingAddedEventArgs { Parent = parentAdapter, TreeItem = newNode };

        vm.InvokeHandleItemBeingAdded(scene, newNode, parentAdapter, args);

        mutator.Verify(m => m.CreateNodeUnderParent(newNode.AttachedObject, parentNode, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_RoutesToOrganizerForFolderParent()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder");

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new SceneNodeAdapter(node);

        var args = new TreeItemBeingAddedEventArgs { Parent = folder, TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, folder, args);

        organizer.Verify(o => o.MoveNodeToFolder(node.Id, folder.Id, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingAdded_WithSceneAdapterParentCreatesNodeAtRoot()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var nodeAdapter = new SceneNodeAdapter(new SceneNode(scene) { Name = "NewNode" });
        var args = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, sceneAdapter, args);

        mutator.Verify(m => m.CreateNodeAtRoot(nodeAdapter.AttachedObject, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_NoSelectionReturnsEmpty()
    {
        var (vm, _, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Single;

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEmpty();
        _ = capture.UsedShownItemsFallback.Should().BeFalse();
    }

    [TestMethod]
    public void OnItemBeingAdded_ReparentsExistingNodeBetweenParents()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

        var parentA = new SceneNode(scene) { Name = "ParentA" };
        var parentB = new SceneNode(scene) { Name = "ParentB" };
        var child = new SceneNode(scene) { Name = "Child" };
        parentA.AddChild(child);
        scene.RootNodes.Add(parentA);
        scene.RootNodes.Add(parentB);

        var parentAAdapter = new SceneNodeAdapter(parentA);
        var parentBAdapter = new SceneNodeAdapter(parentB);
        var childAdapter = new SceneNodeAdapter(child);

        var args = new TreeItemBeingAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter };

        vm.InvokeHandleItemBeingAdded(scene, childAdapter, parentBAdapter, args);

        mutator.Verify(m => m.ReparentNode(child.Id, parentA.Id, parentB.Id, scene), Times.Once);
        mutator.Verify(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()), Times.Never);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingRemoved_WhenDeleteCallsMutatorRemove()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new SceneNodeAdapter(node);

        var args = new TreeItemBeingRemovedEventArgs { TreeItem = nodeAdapter };

        vm.InvokeHandleItemBeingRemoved(scene, nodeAdapter, args);

        mutator.Verify(m => m.RemoveNode(node.Id, scene), Times.Once);
        organizer.Verify(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()), Times.Never);
    }

    [TestMethod]
    public void OnItemBeingRemoved_DuringMoveCapturesOldParentForReparent()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var parentAdapter = new SceneNodeAdapter(parent);
        var childAdapter = new SceneNodeAdapter(child);

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
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var childAdapter = new SceneNodeAdapter(child);

        vm.CaptureOldParentForMove(childAdapter, new SceneNodeAdapter(parent));
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
        var (vm, scene, mutator, organizer, _) = CreateViewModel();
        var folder = new FolderAdapter(Guid.NewGuid(), "Folder");

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);

        var layout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };
        scene.ExplorerLayout = layout;

        var adapter = new SceneNodeAdapter(node);
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
    public async Task MoveAdaptersIntoFolderAsync_SkipsMissingNode()
    {
        var (vm, scene, _, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);
        var folderId = Guid.NewGuid();
        var folderAdapter = new FolderAdapter(folderId, "Folder");
        await vm.AddFolderToSceneAdapter(sceneAdapter, folderAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(folderAdapter).ConfigureAwait(false);

        var folderEntry = new ExplorerEntryData
        {
            FolderId = folderId,
            Name = "Folder",
            Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = Guid.NewGuid() } },
        };

        var moved = await vm.InvokeMoveAdaptersIntoFolderAsync(sceneAdapter, folderAdapter, folderEntry).ConfigureAwait(false);

        _ = moved.Should().Be(0);
        var kids = await folderAdapter.Children.ConfigureAwait(false);
        _ = kids.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task MoveAdaptersIntoFolderAsync_MovesExistingNode(bool preExpanded)
    {
        var (vm, scene, _, _, _) = CreateViewModel();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };

        // Rebuild layout so adapter tree contains the node
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);
        var layoutNode = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<LayoutNodeAdapter>().Single();

        var folderId = Guid.NewGuid();
        var folderAdapter = new FolderAdapter(folderId, "Folder");
        await vm.AddFolderToSceneAdapter(sceneAdapter, folderAdapter).ConfigureAwait(false);
        folderAdapter.IsExpanded = preExpanded;

        var folderEntry = new ExplorerEntryData
        {
            FolderId = folderId,
            Name = "Folder",
            Children = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } },
        };

        var moved = await vm.InvokeMoveAdaptersIntoFolderAsync(sceneAdapter, folderAdapter, folderEntry).ConfigureAwait(false);

        _ = moved.Should().Be(1);
        var kids = await folderAdapter.Children.ConfigureAwait(false);
        _ = kids.Should().Contain(layoutNode);
        _ = folderAdapter.IsExpanded.Should().BeTrue("folder should auto-expand on move when not pre-expanded");
    }

    [TestMethod]
    public async Task ExpandAndSelectFolderAsync_ExpandsExistingFolder()
    {
        var (vm, scene, _, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);

        var folderId = Guid.NewGuid();
        var folderAdapter = new FolderAdapter(folderId, "Folder");
        await vm.AddFolderToSceneAdapter(sceneAdapter, folderAdapter).ConfigureAwait(false);

        var folderEntry = new ExplorerEntryData { FolderId = folderId, Name = "Folder" };

        vm.SelectionMode = SelectionMode.Single;
        await vm.InvokeExpandAndSelectFolderAsync(sceneAdapter, folderAdapter, folderEntry).ConfigureAwait(false);

        _ = folderAdapter.IsExpanded.Should().BeTrue();
        _ = folderAdapter.IsSelected.Should().BeTrue();
    }

    [TestMethod]
    public async Task RemoveSelectedItems_DeletesSelectedNode()
    {
        var (vm, scene, mutator, organizer, _) = CreateViewModel();

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
        var (vm, scene, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Single;

        var adapter = new SceneNodeAdapter(new SceneNode(scene) { Name = "Node" });
        vm.ShownItems.Add(adapter);
        vm.SelectItem(adapter);

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEquivalentTo(new[] { adapter.AttachedObject.Id });
        _ = capture.UsedShownItemsFallback.Should().BeFalse();
    }

    [TestMethod]
    public void CaptureSelectionForFolderCreation_UsesMultipleSelectionModelAndIgnoresFolders()
    {
        var (vm, scene, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.Multiple;

        var nodeAdapter = new SceneNodeAdapter(new SceneNode(scene) { Name = "Node" });
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
        var (vm, scene, _, _, _) = CreateViewModel();
        vm.SelectionMode = SelectionMode.None;

        var shownAdapter = new SceneNodeAdapter(new SceneNode(scene) { Name = "Shown" }) { IsSelected = true };
        vm.ShownItems.Add(shownAdapter);

        var capture = vm.CaptureSelectionForFolderCreation();

        _ = capture.NodeIds.Should().BeEquivalentTo(new[] { shownAdapter.AttachedObject.Id });
        _ = capture.UsedShownItemsFallback.Should().BeTrue();
    }

    [TestMethod]
    public void CreateFolderCreationContext_WhenFolderMissingIdReturnsNull()
    {
        var (vm, scene, _, organizer, _) = CreateViewModel();
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
        var (vm, scene, _, organizer, _) = CreateViewModel();
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
    public async Task CreateFolderFromSelection_PreservesExpansionStateOfMovedNodes()
    {
        var (vm, scene, _, _, _) = CreateViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);

        // Setup: Root -> Parent (Expanded) -> Child
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        // Add Parent to tree (simulating dynamic add or load)
        var parentAdapter = new SceneNodeAdapter(parent);

        await vm.InvokeInsertItemAsync(0, sceneAdapter, parentAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(parentAdapter).ConfigureAwait(false);

        // Verify setup
        _ = parentAdapter.IsExpanded.Should().BeTrue();
        var parentChildren = await parentAdapter.Children.ConfigureAwait(false);
        var childAdapter = parentChildren.OfType<SceneNodeAdapter>().FirstOrDefault(c => c.AttachedObject == child);
        _ = childAdapter.Should().NotBeNull();

        // Select Parent
        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(parentAdapter);

        // Create Folder
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify Parent is inside Folder
        var folder = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().First();
        var movedParent = (await folder.Children.ConfigureAwait(false)).First();

        // Verify Parent is still expanded
        _ = movedParent.IsExpanded.Should().BeTrue("Moved node should remain expanded");

        // Verify Child is still present
        var children = await movedParent.Children.ConfigureAwait(false);
        _ = children.Should().Contain(childAdapter, "Moved node should retain its children");
    }
}
