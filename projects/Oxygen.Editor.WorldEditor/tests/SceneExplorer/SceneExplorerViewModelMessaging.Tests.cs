// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Messages;
using static Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure.SceneExplorerViewModelTestFixture;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneExplorerViewModelMessagingTests
{
    [TestMethod]
    public async Task CreateNodeAtRoot_SendsSceneNodeAddedMessage()
    {
        var (vm, scene, _, _, messenger) = CreateViewModel();
        SceneNodeAddedMessage? capturedMessage = null;
        messenger.Register<SceneNodeAddedMessage>(this, (r, m) => capturedMessage = m);

        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);
        var node = new SceneNode(scene) { Name = "NewNode" };
        var nodeAdapter = new SceneNodeAdapter(node);

        var beingAddedArgs = new TreeItemBeingAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter };
        vm.InvokeHandleItemBeingAdded(scene, nodeAdapter, sceneAdapter, beingAddedArgs);

        var addedArgs = new TreeItemAddedEventArgs { Parent = sceneAdapter, TreeItem = nodeAdapter, RelativeIndex = 0 };
        await vm.InvokeHandleItemAddedAsync(addedArgs);

        Assert.IsNotNull(capturedMessage, "Message was not sent (capturedMessage is null)");
        Assert.IsTrue(capturedMessage.Nodes.Contains(node));
    }

    [TestMethod]
    public async Task ReparentNode_SendsSceneNodeReparentedMessage()
    {
        var (vm, scene, _, _, messenger) = CreateViewModel();
        SceneNodeReparentedMessage? capturedMessage = null;
        messenger.Register<SceneNodeReparentedMessage>(this, (r, m) => capturedMessage = m);

        var parentA = new SceneNode(scene) { Name = "ParentA" };
        var parentB = new SceneNode(scene) { Name = "ParentB" };
        var child = new SceneNode(scene) { Name = "Child" };
        parentA.AddChild(child);
        scene.RootNodes.Add(parentA);
        scene.RootNodes.Add(parentB);

        var parentBAdapter = new SceneNodeAdapter(parentB);
        var childAdapter = new SceneNodeAdapter(child);

        var beingAddedArgs = new TreeItemBeingAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter };
        vm.InvokeHandleItemBeingAdded(scene, childAdapter, parentBAdapter, beingAddedArgs);

        var addedArgs = new TreeItemAddedEventArgs { Parent = parentBAdapter, TreeItem = childAdapter, RelativeIndex = 0 };
        await vm.InvokeHandleItemAddedAsync(addedArgs);

        Assert.IsNotNull(capturedMessage, "Message was not sent (capturedMessage is null)");
        Assert.AreEqual(child, capturedMessage.Node);
        Assert.AreEqual(parentA.Id, capturedMessage.OldParentNodeId);
        Assert.AreEqual(parentB.Id, capturedMessage.NewParentNodeId);
    }

    [TestMethod]
    public async Task RemoveNode_SendsSceneNodeRemovedMessage()
    {
        var (vm, scene, _, _, messenger) = CreateViewModel();
        SceneNodeRemovedMessage? capturedMessage = null;
        messenger.Register<SceneNodeRemovedMessage>(this, (r, m) => capturedMessage = m);

        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var nodeAdapter = new SceneNodeAdapter(node);
        var sceneAdapter = SceneAdapter.BuildLayoutTree(scene);

        var beingRemovedArgs = new TreeItemBeingRemovedEventArgs { TreeItem = nodeAdapter };
        vm.InvokeHandleItemBeingRemoved(scene, nodeAdapter, beingRemovedArgs);

        var removedArgs = new TreeItemRemovedEventArgs { TreeItem = nodeAdapter, Parent = sceneAdapter, RelativeIndex = 0 };
        await vm.InvokeHandleItemRemovedAsync(removedArgs);

        Assert.IsNotNull(capturedMessage, "Message was not sent (capturedMessage is null)");
        Assert.IsTrue(capturedMessage.Nodes.Contains(node));
    }
}
