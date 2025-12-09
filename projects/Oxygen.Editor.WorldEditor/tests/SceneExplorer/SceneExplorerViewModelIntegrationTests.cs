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
        var parentAdapter = new LayoutNodeAdapter(parent);

        await vm.InvokeInsertItemAsync(0, sceneAdapter, parentAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(parentAdapter).ConfigureAwait(false);

        // Verify setup
        _ = parentAdapter.IsExpanded.Should().BeTrue();
        var parentChildren = await parentAdapter.Children.ConfigureAwait(false);
        var childAdapter = parentChildren.OfType<LayoutNodeAdapter>().FirstOrDefault(c => c.AttachedObject == child);
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
        _ = children.Should().Contain(c => MatchesAttachedObject(c, child), "Moved node should retain its children");
    }

    [TestMethod]
    public async Task UndoRedo_CreateFolderFromSelection_RestoresLayout()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, nodeAdapter).ConfigureAwait(false);

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(nodeAdapter);

        // Execute
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        var folder = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().FirstOrDefault();
        _ = folder.Should().NotBeNull();
        _ = (await folder!.Children.ConfigureAwait(false)).Should().Contain(c => MatchesAttachedObject(c, node));

        // Undo
        UndoRedo.Default[vm].Undo();

        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterUndo.OfType<LayoutNodeAdapter>().Should().ContainSingle(c => ReferenceEquals(c.AttachedObject, node));
        _ = childrenAfterUndo.OfType<FolderAdapter>().Should().BeEmpty();

        // Redo
        UndoRedo.Default[vm].Redo();

        var childrenAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterRedo = childrenAfterRedo.OfType<FolderAdapter>().FirstOrDefault();
        _ = folderAfterRedo.Should().NotBeNull();
        var folderChildrenAfterRedo = await folderAfterRedo!.Children.ConfigureAwait(false);
        _ = folderChildrenAfterRedo.Should().Contain(c => MatchesAttachedObject(c, node));
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
    public async Task CreateFolderFromSelectionCommand_UndoRedo_PreservesFolderExpansion()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new LayoutNodeAdapter(node);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, nodeAdapter).ConfigureAwait(false);

        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(nodeAdapter);

        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        var childrenAfterFolder = await sceneAdapter.Children.ConfigureAwait(false);
        var folder = childrenAfterFolder.OfType<FolderAdapter>().First();

        // Expand the new folder and ensure it sticks
        folder.IsExpanded = true;
        await vm.ExpandItemForTestAsync(folder).ConfigureAwait(false);
        _ = folder.IsExpanded.Should().BeTrue();

        // Undo creation removes folder
        UndoRedo.Default[vm].Undo();
        var rootsAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = rootsAfterUndo.OfType<FolderAdapter>().Should().BeEmpty();

        // Redo should recreate folder and keep it expanded
        UndoRedo.Default[vm].Redo();
        var rootsAfterRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterRedo = rootsAfterRedo.OfType<FolderAdapter>().First();
        _ = folderAfterRedo.IsExpanded.Should().BeTrue("Folder expansion state should be preserved after redo");
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

    [TestMethod]
    public async Task CreateFolderFromSelectionCommand_UndoRedo_MultipleRoundsWithMultipleNodes()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        var sibling = new SceneNode(scene) { Name = "Sibling" };
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(sibling);

        var parentAdapter = new LayoutNodeAdapter(parent);
        var siblingAdapter = new LayoutNodeAdapter(sibling);
        await vm.InvokeInsertItemAsync(0, sceneAdapter, parentAdapter).ConfigureAwait(false);
        await vm.InvokeInsertItemAsync(1, sceneAdapter, siblingAdapter).ConfigureAwait(false);
        await vm.ExpandItemForTestAsync(parentAdapter).ConfigureAwait(false);

        vm.SelectionMode = SelectionMode.Multiple;
        vm.SelectItem(parentAdapter);
        vm.SelectItem(siblingAdapter);

        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        async Task AssertFolderStateAsync()
        {
            var rootChildren = await sceneAdapter.Children.ConfigureAwait(false);
            var folder = rootChildren.OfType<FolderAdapter>().FirstOrDefault();
            _ = folder.Should().NotBeNull();
            var folderKids = await folder!.Children.ConfigureAwait(false);
            _ = folderKids.Should().Contain(k => MatchesAttachedObject(k, parent));
            _ = folderKids.Should().Contain(k => MatchesAttachedObject(k, sibling));

            var parentInFolder = folderKids.OfType<LayoutNodeAdapter>().First(k => ReferenceEquals(k.AttachedObject, parent));
            var parentChildren = await parentInFolder.Children.ConfigureAwait(false);
            _ = parentChildren.Should().Contain(c => MatchesAttachedObject(c, child));
        }

        async Task AssertRootStateAsync()
        {
            var rootChildren = await sceneAdapter.Children.ConfigureAwait(false);
            _ = rootChildren.OfType<FolderAdapter>().Should().BeEmpty();
            _ = rootChildren.OfType<LayoutNodeAdapter>().Should()
                .Contain(c => ReferenceEquals(((LayoutNodeAdapter)c).AttachedObject, parent))
                .And.Contain(c => ReferenceEquals(((LayoutNodeAdapter)c).AttachedObject, sibling));
        }

        await AssertFolderStateAsync().ConfigureAwait(false);

        for (var i = 0; i < 2; i++)
        {
            UndoRedo.Default[vm].Undo();
            await AssertRootStateAsync().ConfigureAwait(false);

            UndoRedo.Default[vm].Redo();
            await AssertFolderStateAsync().ConfigureAwait(false);
        }
    }

    [TestMethod]
    public async Task UndoRedo_MultipleFoldersAndSiblings_RoundTripsLayouts()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        var nodeA = new SceneNode(scene) { Name = "A" };
        var nodeB = new SceneNode(scene) { Name = "B" };
        var nodeC = new SceneNode(scene) { Name = "C" };
        scene.RootNodes.Add(nodeA);
        scene.RootNodes.Add(nodeB);
        scene.RootNodes.Add(nodeC);

        var adapterA = new LayoutNodeAdapter(nodeA);
        var adapterB = new LayoutNodeAdapter(nodeB);
        var adapterC = new LayoutNodeAdapter(nodeC);

        await vm.InvokeInsertItemAsync(0, sceneAdapter, adapterA).ConfigureAwait(false);
        await vm.InvokeInsertItemAsync(1, sceneAdapter, adapterB).ConfigureAwait(false);
        await vm.InvokeInsertItemAsync(2, sceneAdapter, adapterC).ConfigureAwait(false);

        // Create Folder1 with A
        vm.SelectionMode = SelectionMode.Single;
        vm.SelectItem(adapterA);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Create Folder2 with B
        vm.SelectItem(adapterB);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        async Task AssertTwoFoldersAsync()
        {
            var roots = await sceneAdapter.Children.ConfigureAwait(false);
            _ = roots.OfType<FolderAdapter>().Should().HaveCount(2);
            _ = roots.OfType<LayoutNodeAdapter>().Should().ContainSingle(lna => ReferenceEquals(lna.AttachedObject, nodeC));
        }

        await AssertTwoFoldersAsync().ConfigureAwait(false);

        // Undo both folder creations
        UndoRedo.Default[vm].Undo(); // undo folder2
        UndoRedo.Default[vm].Undo(); // undo folder1

        var rootsAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = rootsAfterUndo.OfType<FolderAdapter>().Should().BeEmpty();
        _ = rootsAfterUndo.OfType<LayoutNodeAdapter>().Should()
            .Contain(c => ReferenceEquals(((LayoutNodeAdapter)c).AttachedObject, nodeA))
            .And.Contain(c => ReferenceEquals(((LayoutNodeAdapter)c).AttachedObject, nodeB))
            .And.Contain(c => ReferenceEquals(((LayoutNodeAdapter)c).AttachedObject, nodeC));

        // Redo both folder creations
        UndoRedo.Default[vm].Redo();
        UndoRedo.Default[vm].Redo();

        await AssertTwoFoldersAsync().ConfigureAwait(false);

        // One more undo/redo round-trip to catch stack stability
        UndoRedo.Default[vm].Undo();
        UndoRedo.Default[vm].Redo();

        await AssertTwoFoldersAsync().ConfigureAwait(false);
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
        var nodeAdapter = (ITreeItem?)children.OfType<LayoutNodeAdapter>().FirstOrDefault();

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
            Type = "Folder",
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

        // Assert folder inserted at root
        var children = sceneAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult();
        var folderAdapter = children.OfType<FolderAdapter>().FirstOrDefault();
        _ = folderAdapter.Should().NotBeNull();

        // Assert node moved into folder (by payload, adapter identity may differ after reconcile/refresh)
        var folderKids = folderAdapter.Children.ConfigureAwait(false).GetAwaiter().GetResult();
        _ = folderKids.Should().Contain(k => MatchesAttachedObject(k, layoutNode.AttachedObject));

        // Assert first undo: folder removed, layout restored
        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(layoutChange.PreviousLayout, options => options.WithStrictOrdering());

        // Redo sequence restores folder and children move
        UndoRedo.Default[vm].Redo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(layoutChange.NewLayout, options => options.WithStrictOrdering());
    }

    private static Guid GetNodeId(ITreeItem item) => item switch
    {
        LayoutNodeAdapter lna => lna.AttachedObject.Id,
        FolderAdapter folder => folder.Children.ConfigureAwait(false).GetAwaiter().GetResult()
            .Select(GetNodeId)
            .FirstOrDefault(),
        _ => throw new InvalidOperationException("Unsupported tree item type"),
    };

    private static bool MatchesAttachedObject(ITreeItem item, SceneNode expected)
    {
        return item is LayoutNodeAdapter lna && ReferenceEquals(lna.AttachedObject, expected);
    }

    [TestMethod]
    public async Task UndoRedo_PreservesExpansionState_WhenRestoringLayout()
    {
        var (vm, scene, _, _, _, _) = SceneExplorerViewModelTestFixture.CreateIntegrationViewModel();
        await vm.HandleDocumentOpenedForTestAsync(scene).ConfigureAwait(false);
        var sceneAdapter = vm.Scene!;

        // Setup: Root -> Node1
        var node1 = new SceneNode(scene) { Name = "Node1" };
        scene.RootNodes.Add(node1);
        var node1Adapter = new LayoutNodeAdapter(node1);
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
        var node2Adapter = new LayoutNodeAdapter(node2);
        await vm.InvokeInsertItemAsync(1, sceneAdapter, node2Adapter).ConfigureAwait(false);

        // Action: Create Folder2 from Node2
        vm.SelectItem(node2Adapter);
        await vm.CreateFolderFromSelectionCommand.ExecuteAsync(null).ConfigureAwait(false);

        // Verify Folder1 is still expanded
        folder1 = (await sceneAdapter.Children.ConfigureAwait(false)).OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue();

        // Undo (Removes Folder2)
        UndoRedo.Default[vm].Undo();

        // Verify Folder1 is STILL expanded
        var children = await sceneAdapter.Children.ConfigureAwait(false);
        folder1 = children.OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue("Folder1 should remain expanded after Undo");

        // Redo (Re-creates Folder2)
        UndoRedo.Default[vm].Redo();

        // Verify Folder1 is STILL expanded
        children = await sceneAdapter.Children.ConfigureAwait(false);
        folder1 = children.OfType<FolderAdapter>().First(f => f.ChildAdapters.OfType<LayoutNodeAdapter>().Any(c => c.AttachedObject == node1));
        _ = folder1.IsExpanded.Should().BeTrue("Folder1 should remain expanded after Redo");
    }
}
