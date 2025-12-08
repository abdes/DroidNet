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
public class SceneExplorerViewModelUndoRedoTests
{
    [TestMethod]
    public async Task RemoveSelectedItems_UndoRedo_RestoresAndRemovesNode()
    {
        var (vm, scene, _, _, _) = SceneExplorerViewModelTestFixture.CreateViewModel();

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
        _ = vm.UndoStack.Count.Should().Be(3);
        _ = vm.RedoStack.Count.Should().Be(0);

        UndoRedo.Default[vm].Undo();
        var childrenAfterUndo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterFirstUndo = childrenAfterUndo.OfType<FolderAdapter>().Single();
        await vm.ExpandItemForTestAsync(folderAfterFirstUndo).ConfigureAwait(false);
        var folderAfterFirstUndoKids = await folderAfterFirstUndo.Children.ConfigureAwait(false);
        _ = folderAfterFirstUndoKids.Should().BeEmpty();
        _ = childrenAfterUndo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(1);

        // Second undo removes the folder entirely, leaving the node at root
        UndoRedo.Default[vm].Undo();
        var childrenAfterSecondUndo = await sceneAdapter.Children.ConfigureAwait(false);
        _ = childrenAfterSecondUndo.OfType<FolderAdapter>().Should().BeEmpty();
        _ = childrenAfterSecondUndo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(1);
        _ = vm.RedoStack.Count.Should().Be(2);

        // First redo reintroduces the empty folder while node stays at root
        UndoRedo.Default[vm].Redo();
        var childrenAfterFirstRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterFirstRedo = childrenAfterFirstRedo.OfType<FolderAdapter>().Single();
        await vm.ExpandItemForTestAsync(folderAfterFirstRedo).ConfigureAwait(false);
        var folderKidsAfterFirstRedo = await folderAfterFirstRedo.Children.ConfigureAwait(false);
        _ = folderKidsAfterFirstRedo.Should().BeEmpty();
        _ = childrenAfterFirstRedo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(2);
        _ = vm.RedoStack.Count.Should().Be(1);

        UndoRedo.Default[vm].Redo();
        var childrenAfterSecondRedo = await sceneAdapter.Children.ConfigureAwait(false);
        var folderAfterSecondRedo = childrenAfterSecondRedo.OfType<FolderAdapter>().Single();
        await vm.ExpandItemForTestAsync(folderAfterSecondRedo).ConfigureAwait(false);
        var folderKidsAfterSecondRedo = await folderAfterSecondRedo.Children.ConfigureAwait(false);
        _ = folderKidsAfterSecondRedo.Any(item => GetNodeId(item) == nodeId).Should().BeTrue();
        _ = vm.UndoStack.Count.Should().Be(3);
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
        var (vm, scene, _, _, _) = SceneExplorerViewModelTestFixture.CreateViewModel();
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
        var (vm, scene, _, _, _) = SceneExplorerViewModelTestFixture.CreateViewModel();
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
        var (vm, scene, _, organizer, _) = SceneExplorerViewModelTestFixture.CreateViewModel();
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

        // Assert first undo: folder remains, children moved out
        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().NotBeNull();
        _ = scene.ExplorerLayout!.Count.Should().Be(2);
        _ = scene.ExplorerLayout.Should().Contain(e => string.Equals(e.Type, "Folder", StringComparison.OrdinalIgnoreCase));
        _ = scene.ExplorerLayout.Should().Contain(e => e.NodeId == node.Id);

        // Assert second undo: folder removed, layout restored
        UndoRedo.Default[vm].Undo();
        _ = scene.ExplorerLayout.Should().BeEquivalentTo(layoutChange.PreviousLayout, options => options.WithStrictOrdering());

        // Redo sequence restores folder and children move
        UndoRedo.Default[vm].Redo();
        _ = scene.ExplorerLayout.Should().NotBeNull();
        _ = scene.ExplorerLayout!.Count.Should().Be(2);
        _ = scene.ExplorerLayout.Should().Contain(e => string.Equals(e.Type, "Folder", StringComparison.OrdinalIgnoreCase));
        _ = scene.ExplorerLayout.Should().Contain(e => e.NodeId == node.Id);

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
}
