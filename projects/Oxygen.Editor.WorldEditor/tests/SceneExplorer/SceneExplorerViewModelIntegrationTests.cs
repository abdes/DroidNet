// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.TimeMachine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneExplorerViewModelIntegrationTests
{
    [TestMethod]
    public async Task CreateFolderFromSelection_CreatesFolderAndMovesItems()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
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
        _ = children.Should().Contain(c => c is LayoutNodeAdapter && ((LayoutNodeAdapter)c).AttachedObject.AttachedObject == child, "Moved node should retain its children");
    }

    [TestMethod]
    public async Task UndoRedo_CreateFolderFromSelection_RestoresLayout()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new SceneNodeAdapter(node);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, nodeAdapter).ConfigureAwait(false);

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(nodeAdapter);

        // Execute
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        var folder = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().FirstOrDefault();
        _ = folder.Should().NotBeNull();
        _ = (await folder!.Children.ConfigureAwait(false)).Should().Contain(c => c is LayoutNodeAdapter && ((LayoutNodeAdapter)c).AttachedObject.AttachedObject == node);

        // Undo
        UndoRedo.Default[vm].Undo();

        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.OfType<FolderAdapter>().Should().BeEmpty();
        _ = childrenAfterUndo.OfType<LayoutNodeAdapter>().Any(n => n.AttachedObject.AttachedObject == node).Should().BeTrue();

        // Redo
        UndoRedo.Default[vm].Redo();

        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterRedo = childrenAfterRedo.OfType<FolderAdapter>().FirstOrDefault();
        _ = folderAfterRedo.Should().NotBeNull();
        _ = (await folderAfterRedo!.Children.ConfigureAwait(false)).Should().Contain(c => c is LayoutNodeAdapter && ((LayoutNodeAdapter)c).AttachedObject.AttachedObject == node);
    }

    [TestMethod]
    public async Task RemoveSelectedItems_UndoRedo_RestoresAndRemovesNode()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        scene.ExplorerLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        await vm.InitializeSceneAsync(sceneAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);
        var layoutNode = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<LayoutNodeAdapter>().Single();

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(layoutNode);

        await vm.RemoveSelectedItemsForTestAsync().ConfigureAwait(false);

        // After delete: node removed, undo stack populated, redo empty
        var childrenAfterDelete = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterDelete.Should().NotContain(layoutNode);
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(0);

        // Undo restores node and populates redo
        UndoRedo.Default[vm].Undo();
        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.Should().Contain(layoutNode);
        _ = vm.UndoStack.Count.Should().Be(0);
        _ = vm.RedoStack.Count.Should().Be(1);

        // Redo removes node again and restores undo entry
        UndoRedo.Default[vm].Redo();
        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterRedo.Should().NotContain(layoutNode);
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(0);
    }

    [TestMethod]
    public async Task AddEntityCommand_UndoRedo_RestoresRootNode()
    {
        var (vm, sceneAdapter, nodeAdapter, _) = await CreateSceneWithSingleNodeAsync().ConfigureAwait(false);

        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(0);

        UndoRedo.Default[vm].Undo();
        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.Should().BeEmpty();
        _ = vm.UndoStack.Count.Should().Be(0);
        _ = vm.RedoStack.Count.Should().Be(1);

        UndoRedo.Default[vm].Redo();
        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterRedo.Should().Contain(nodeAdapter);
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(0);
    }

    [TestMethod]
    public async Task CreateFolderFromSelectionCommand_UndoRedo_MovesNodeInAndOut()
    {
        var (vm, sceneAdapter, nodeAdapter, nodeId) = await CreateSceneWithSingleNodeAsync().ConfigureAwait(false);

        vm.SelectItem(nodeAdapter);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        var childrenAfterFolder = await sceneAdapter.Children.ConfigureAwait(false);
        var folder = childrenAfterFolder.OfType<FolderAdapter>().Single();
        await vm.ExpandItemForTestAsync(folder).ConfigureAwait(false);
        var folderKids = await folder.Children.ConfigureAwait(false);
        _ = folderKids.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(0);

        UndoRedo.Default[vm].Undo();
        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.OfType<FolderAdapter>().Should().BeEmpty();
        _ = childrenAfterUndo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(1);

        // Redo reintroduces the folder with the node
        UndoRedo.Default[vm].Redo();
        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterRedo = childrenAfterRedo.OfType<FolderAdapter>().Single();
        await vm.ExpandItemForTestAsync(folderAfterRedo).ConfigureAwait(false);
        var folderKidsAfterRedo = await folderAfterRedo.Children.ConfigureAwait(false);
        _ = folderKidsAfterRedo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(0);
    }

    [TestMethod]
    public async Task RemoveSelectedItemsCommand_UndoRedo_RemovesAndRestoresNode()
    {
        var (vm, sceneAdapter, nodeAdapter, nodeId) = await CreateSceneWithSingleNodeAsync().ConfigureAwait(false);

        vm.SelectItem(nodeAdapter);
        await vm.RemoveSelectedItemsCommand.ExecuteAsync(null).ConfigureAwait(false);

        var childrenAfterDelete = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterDelete.Should().NotContain(nodeAdapter);
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(0);

        UndoRedo.Default[vm].Undo();
        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.Should().Contain(nodeAdapter);
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(1);

        UndoRedo.Default[vm].Redo();
        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterRedo.Should().NotContain(nodeAdapter);
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(0);
    }

    private static async Task<(SceneExplorerViewModelTestFixture.TestSceneExplorerViewModel vm, SceneAdapter sceneAdapter, ITreeItem nodeAdapter, Guid nodeId)> CreateSceneWithSingleNodeAsync()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);

        var sceneAdapter = vm.Scene ?? throw new InvalidOperationException("Scene not initialized");

        vm.SelectionMode = SelectionMode.Single;
        await vm.AddEntityCommand.ExecuteAsync(null).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(sceneAdapter).ConfigureAwait(false);

        var children = await sceneAdapter.Children.ConfigureAwait(false);
        var nodeAdapter = (ITreeItem?)children.OfType<SceneNodeAdapter>().FirstOrDefault()
                        ?? children.OfType<LayoutNodeAdapter>().First();

        var nodeId = GetNodeId(nodeAdapter ?? throw new InvalidOperationException("Node not created"));
        return (vm, sceneAdapter, nodeAdapter, nodeId);
    }

    [TestMethod]
    public void RegisterCreateFolderUndo_RestoresLayoutsOnUndoRedo()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var previousLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = Guid.NewGuid() } };
        var newLayout = new List<ExplorerEntryData> { new() { Type = "Folder", FolderId = Guid.NewGuid(), Name = "Folder" } };
        var change = new LayoutChangeRecord("CreateFolder", previousLayout, newLayout, newLayout[0]);

        scene.ExplorerLayout = newLayout;

        vm.InvokeRegisterCreateFolderUndo(sceneAdapter, scene, change, "Folder");

        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(previousLayout, options => options.WithStrictOrdering());

        UndoRedo.Default[vm].Redo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(newLayout, options => options.WithStrictOrdering());
    }

    [TestMethod]
    public void ApplyFolderCreationAsync_InsertsFolderMovesChildrenAndRegistersUndo()
    {
        var (vm, scene, _, organizer, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var initialLayout = new List<ExplorerEntryData> { new() { Type = "Node", NodeId = node.Id } };
        scene.ExplorerLayout = initialLayout;

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var layoutNode = sceneAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult()
            .OfType<LayoutNodeAdapter>()
            .Single();

        var folderId = Guid.NewGuid();
        var folderEntry = new ExplorerEntryData
        {
            FolderId = folderId,
            Name = "Folder",
            Children = new List<ExplorerEntryData> { new() { NodeId = node.Id, Type = "Node" } },
        };
        var newLayout = new List<ExplorerEntryData> { folderEntry };
        var layoutChange = new LayoutChangeRecord("CreateFolder", initialLayout, newLayout, folderEntry);
        scene.ExplorerLayout = newLayout;

        var context = new SceneExplorerViewModel.FolderCreationContext(
            layoutChange,
            new FolderAdapter(folderId, "Folder"),
            folderEntry);

        // Act
        vm.ApplyFolderCreationAsync(sceneAdapter, scene, context).GetAwaiter().GetResult();

        // Assert folder inserted at index 0
        var children = sceneAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult();
        _ = children[0].Should().BeOfType<FolderAdapter>();

        // Assert node moved into folder
        var folderAdapter = (FolderAdapter)children[0];
        var folderKids = folderAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult();
        _ = folderKids.Should().Contain(layoutNode);

        // Assert first undo: folder removed, layout restored
        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(layoutChange.PreviousLayout, options => options.WithStrictOrdering());

        // Redo sequence restores folder and children move
        UndoRedo.Default[vm].Redo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(layoutChange.NewLayout, options => options.WithStrictOrdering());
    }

    private static Guid GetNodeId(ITreeItem item) => item switch
    {
        SceneNodeAdapter sna => sna.AttachedObject.Id,
        LayoutNodeAdapter lna => lna.AttachedObject.AttachedObject.Id,
        FolderAdapter folder => folder.Children.ConfigureAwait(false).GetAwaiter().GetResult()
            .Select(GetNodeId)
            .FirstOrDefault(),
        _ => throw new InvalidOperationException("Unsupported tree item type"),
    };

    [TestMethod]
    public async Task UndoRedo_PreservesExpansionState_WhenRestoringLayout()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        // Setup: Root -> Node1
        var node1 = new SceneNode(scene) { Name = "Node1" };
        scene.RootNodes.Add(node1);
        var node1Adapter = new SceneNodeAdapter(node1);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, node1Adapter).ConfigureAwait(false);

        // Create Folder1 from Node1
        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(node1Adapter);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        var folder1 = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().First();
        folder1.IsExpanded = true; // Ensure it is expanded

        // Setup: Root -> Node2
        var node2 = new SceneNode(scene) { Name = "Node2" };
        scene.RootNodes.Add(node2);
        var node2Adapter = new SceneNodeAdapter(node2);
        await vm.InvokeInsertItemAsync(1, sceneAdapter, node2Adapter).ConfigureAwait(false);

        // Action: Create Folder2 from Node2
        vm.SelectItem(node2Adapter);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify Folder1 is still expanded
        folder1 = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue();

        // Undo (Removes Folder2)
        UndoRedo.Default[vm].Undo();

        // Verify Folder1 is STILL expanded
        var children = await sceneAdapter.Children.ConfigureAwait(false);
        folder1 = children.OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue("Folder1 should remain expanded after Undo");

        // Redo (Re-creates Folder2)
        UndoRedo.Default[vm].Redo();

        // Verify Folder1 is STILL expanded
        children = await sceneAdapter.Children.ConfigureAwait(false);
        folder1 = children.OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue("Folder1 should remain expanded after Redo");
    }
}
