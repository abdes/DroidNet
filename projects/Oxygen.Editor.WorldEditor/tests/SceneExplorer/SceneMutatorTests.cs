// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests;

[TestClass]
public class SceneMutatorTests
{
    private readonly SceneMutator mutator = new(NullLogger<SceneMutator>.Instance);

    [TestMethod]
    public void CreateNodeAtRoot_AddsNodeAndClearsParent()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        scene.RootNodes.Add(parent);

        var node = new SceneNode(scene) { Name = "Child" };
        node.SetParent(parent);

        var change = this.mutator.CreateNodeAtRoot(node, scene);

        _ = change.OldParentId.Should().Be(parent.Id);
        _ = change.NewParentId.Should().BeNull();
        _ = change.RequiresEngineSync.Should().BeTrue();
        _ = change.AddedToRootNodes.Should().BeTrue();
        _ = change.RemovedFromRootNodes.Should().BeFalse();
        _ = node.Parent.Should().BeNull();
        _ = scene.RootNodes.Should().Contain(node);
    }

    [TestMethod]
    public void CreateNodeUnderParent_RemovesFromRootNodes()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(node);

        var change = this.mutator.CreateNodeUnderParent(node, parent, scene);

        _ = change.NewParentId.Should().Be(parent.Id);
        _ = change.RemovedFromRootNodes.Should().BeTrue();
        _ = change.AddedToRootNodes.Should().BeFalse();
        _ = node.Parent.Should().Be(parent);
        _ = scene.RootNodes.Should().NotContain(node);
    }

    [TestMethod]
    public void RemoveNode_RemovesFromParentAndRootNodes()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var node = new SceneNode(scene) { Name = "Node" };
        parent.AddChild(node);
        scene.RootNodes.Add(parent);

        var change = this.mutator.RemoveNode(node.Id, scene);

        _ = change.OldParentId.Should().Be(parent.Id);
        _ = node.Parent.Should().BeNull();
        _ = scene.RootNodes.Should().NotContain(node);
        _ = change.RequiresEngineSync.Should().BeTrue();
        _ = change.AddedToRootNodes.Should().BeFalse();
    }

    [TestMethod]
    public void RemoveNode_RemovesRootNode()
    {
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        scene.RootNodes.Add(root);

        var change = this.mutator.RemoveNode(root.Id, scene);

        _ = change.OldParentId.Should().BeNull();
        _ = scene.RootNodes.Should().NotContain(root);
        _ = change.RemovedFromRootNodes.Should().BeTrue();
    }

    [TestMethod]
    public void RemoveHierarchy_RemovesRootAndKeepsChildrenAttachedForEngineSync()
    {
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        root.AddChild(child);
        scene.RootNodes.Add(root);

        var change = this.mutator.RemoveHierarchy(root.Id, scene);

        _ = change.OperationName.Should().Be("RemoveHierarchy");
        _ = change.AffectedNode.Should().Be(root);
        _ = change.OldParentId.Should().BeNull();
        _ = change.RemovedFromRootNodes.Should().BeTrue();
        _ = root.Parent.Should().BeNull();
        _ = child.Parent.Should().Be(root);
        _ = scene.RootNodes.Should().NotContain(root);
    }

    [TestMethod]
    public void ReparentNode_ToRoot_AddsToRootNodes()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);

        var change = this.mutator.ReparentNode(child.Id, oldParentId: parent.Id, newParentId: null, scene);

        _ = child.Parent.Should().BeNull();
        _ = scene.RootNodes.Should().Contain(child);
        _ = change.AddedToRootNodes.Should().BeTrue();
        _ = change.RemovedFromRootNodes.Should().BeFalse();
    }

    [TestMethod]
    public void ReparentNode_ToNode_RemovesFromRootNodes()
    {
        var scene = CreateScene();
        var newParent = new SceneNode(scene) { Name = "NewParent" };
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(newParent);
        scene.RootNodes.Add(node);

        var change = this.mutator.ReparentNode(node.Id, oldParentId: null, newParentId: newParent.Id, scene);

        _ = node.Parent.Should().Be(newParent);
        _ = scene.RootNodes.Should().NotContain(node);
        _ = change.AddedToRootNodes.Should().BeFalse();
        _ = change.RemovedFromRootNodes.Should().BeTrue();
    }

    [TestMethod]
    public void ReparentHierarchies_ReparentsAllToTargetParent()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child1 = new SceneNode(scene) { Name = "Child1" };
        var child2 = new SceneNode(scene) { Name = "Child2" };
        scene.RootNodes.Add(parent);
        scene.RootNodes.Add(child1);
        scene.RootNodes.Add(child2);

        var changes = this.mutator.ReparentHierarchies(new[] { child1.Id, child2.Id }, parent.Id, scene);

        _ = changes.Should().HaveCount(2);
        _ = child1.Parent.Should().Be(parent);
        _ = child2.Parent.Should().Be(parent);
        _ = scene.RootNodes.Should().NotContain(child1);
        _ = scene.RootNodes.Should().NotContain(child2);
        _ = changes.All(c => c.NewParentId == parent.Id).Should().BeTrue();
    }

    [TestMethod]
    public void ReparentHierarchies_MovesRootsAndKeepsSubtreeIntact()
    {
        var scene = CreateScene();
        var hierarchyRoot = new SceneNode(scene) { Name = "HierarchyRoot" };
        var child = new SceneNode(scene) { Name = "Child" };
        hierarchyRoot.AddChild(child);
        var newParent = new SceneNode(scene) { Name = "NewParent" };
        scene.RootNodes.Add(hierarchyRoot);
        scene.RootNodes.Add(newParent);

        var changes = this.mutator.ReparentHierarchies(new[] { hierarchyRoot.Id }, newParent.Id, scene);

        _ = changes.Should().HaveCount(1);
        var change = changes[0];
        _ = change.NewParentId.Should().Be(newParent.Id);
        _ = change.RemovedFromRootNodes.Should().BeTrue();
        _ = hierarchyRoot.Parent.Should().Be(newParent);
        _ = child.Parent.Should().Be(hierarchyRoot, "reparenting should keep subtree attached");
        _ = scene.RootNodes.Should().Contain(newParent);
        _ = scene.RootNodes.Should().NotContain(hierarchyRoot);
    }

    [TestMethod]
    public void RemoveNode_ThrowsWhenNotFound()
    {
        var scene = CreateScene();
        Action act = () => _ = this.mutator.RemoveNode(Guid.NewGuid(), scene);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "TestScene" };
    }
}
