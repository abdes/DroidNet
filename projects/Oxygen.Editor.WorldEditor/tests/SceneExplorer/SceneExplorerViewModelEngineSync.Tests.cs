// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Services;
using Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure;
using static Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure.SceneExplorerViewModelTestFixture;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneExplorerViewModelEngineSyncTests
{
    [TestMethod]
    public async Task CreateNodeAtRoot_CallsEngineSync()
    {
        var (vm, scene, mutator, _, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var node = new SceneNode(scene) { Name = "NewNode" };
        var nodeAdapter = new SceneNodeAdapter(node);

        // 1. Simulate "Being Added" to queue the change
        var beingAddedArgs = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter };
        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, sceneAdapter, beingAddedArgs);

        // Verify mutator was called and change is pending (implicit via VM logic)
        mutator.Verify(m => m.CreateNodeAtRoot(node, scene), Times.Once);

        // 2. Simulate "Added" to trigger engine sync
        var addedArgs = new TreeItemAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter, RelativeIndex = 0 };
        await vm.InvokeHandleItemAddedAsync(addedArgs);

        // 3. Verify Engine Sync
        // Note: The fixture sets up mutator to return a record with RequiresEngineSync=true
        var engineSync = GetEngineSyncMock(vm);
        engineSync.Verify(es => es.CreateNodeAsync(node, null), Times.Once);
    }

    [TestMethod]
    public async Task CreateNodeUnderParent_CallsEngineSync()
    {
        var (vm, scene, mutator, _, _, _) = CreateViewModel();
        var parent = new SceneNode(scene) { Name = "Parent" };
        scene.RootNodes.Add(parent);
        var parentAdapter = new SceneNodeAdapter(parent);

        var node = new SceneNode(scene) { Name = "Child" };
        var nodeAdapter = new SceneNodeAdapter(node);

        // 1. Simulate "Being Added"
        var beingAddedArgs = new TreeItemBeingAddedEventArgs { Parent = parentAdapter, TreeItem = nodeAdapter };
        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, parentAdapter, beingAddedArgs);

        mutator.Verify(m => m.CreateNodeUnderParent(node, parent, scene), Times.Once);

        // 2. Simulate "Added"
        var addedArgs = new TreeItemAddedEventArgs { Parent = parentAdapter, TreeItem = nodeAdapter, RelativeIndex = 0 };
        await vm.InvokeHandleItemAddedAsync(addedArgs);

        // 3. Verify Engine Sync
        var engineSync = GetEngineSyncMock(vm);
        engineSync.Verify(es => es.CreateNodeAsync(node, parent.Id), Times.Once);
    }

    [TestMethod]
    public async Task ReparentNode_CallsEngineSync()
    {
        var (vm, scene, mutator, _, _, _) = CreateViewModel();
        var parentA = new SceneNode(scene) { Name = "ParentA" };
        var parentB = new SceneNode(scene) { Name = "ParentB" };
        var child = new SceneNode(scene) { Name = "Child" };
        parentA.AddChild(child);
        scene.RootNodes.Add(parentA);
        scene.RootNodes.Add(parentB);

        var parentBAdapter = new SceneNodeAdapter(parentB);
        var childAdapter = new SceneNodeAdapter(child);

        // 1. Simulate "Being Added" (Reparenting)
        var beingAddedArgs = new TreeItemBeingAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter };
        vm.InvokeHandleItemBeingAdded(scene, childAdapter, parentBAdapter, beingAddedArgs);

        mutator.Verify(m => m.ReparentNode(child.Id, parentA.Id, parentB.Id, scene), Times.Once);

        // 2. Simulate "Added"
        var addedArgs = new TreeItemAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter, RelativeIndex = 0 };

        // The node must be active for engine sync to be called
        child.IsActive = true;

        await vm.InvokeHandleItemAddedAsync(addedArgs);

        // 3. Verify Engine Sync
        var engineSync = GetEngineSyncMock(vm);
        // Note: The fixture sets up mutator to return a record with RequiresEngineSync=true
        engineSync.Verify(es => es.ReparentNodeAsync(child.Id, parentB.Id), Times.Once);
    }

    [TestMethod]
    public async Task RemoveNode_CallsEngineSync()
    {
        var (vm, scene, mutator, _, _, _) = CreateViewModel();
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new SceneNodeAdapter(node);

        // 1. Simulate "Being Removed"
        var beingRemovedArgs = new TreeItemBeingRemovedEventArgs { TreeItem = nodeAdapter };
        vm.InvokeHandleItemBeingRemoved(scene, nodeAdapter, beingRemovedArgs);

        mutator.Verify(m => m.RemoveNode(node.Id, scene), Times.Once);

        // 2. Simulate "Removed"
        var removedArgs = new TreeItemRemovedEventArgs { TreeItem = nodeAdapter, Parent = sceneAdapter, RelativeIndex = 0 };

        // The node must be active for engine sync to be called
        node.IsActive = true;

        await vm.InvokeHandleItemRemovedAsync(removedArgs);

        // 3. Verify Engine Sync
        var engineSync = GetEngineSyncMock(vm);
        engineSync.Verify(es => es.RemoveNodeAsync(node.Id), Times.Once);
    }

    private static Mock<ISceneEngineSync> GetEngineSyncMock(TestSceneExplorerViewModel vm)
    {
        // Reflection to get the private engineSync field
        var field = typeof(SceneExplorerViewModel).GetField("sceneEngineSync", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        return Mock.Get((ISceneEngineSync)field!.GetValue(vm)!);
    }
}
