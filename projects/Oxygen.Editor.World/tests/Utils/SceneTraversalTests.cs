// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Utils;

/// <summary>
/// Verifies hierarchy traversal, async traversal, collection, and search methods on <see cref="SceneTraversal"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneTraversal))]
public sealed class SceneTraversalTests
{
    [TestMethod]
    public void TraverseDepthFirst_VisitsNodesInDepthFirstOrderWithCorrectParentIds()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var childA = new SceneNode(scene) { Name = "ChildA" };
        var childB = new SceneNode(scene) { Name = "ChildB" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(childA);
        childA.AddChild(grandChild);
        root.AddChild(childB);

        var visited = new List<(SceneNode Node, NodeId? ParentId)>();
        SceneTraversal.TraverseDepthFirst(root, (node, parentId) => visited.Add((node, parentId)));

        _ = visited.Should().HaveCount(4);
        _ = visited[0].Node.Should().BeSameAs(root);
        _ = visited[0].ParentId.Should().BeNull();
        _ = visited[1].Node.Should().BeSameAs(childA);
        _ = visited[1].ParentId.Should().Be(root.Id);
        _ = visited[2].Node.Should().BeSameAs(grandChild);
        _ = visited[2].ParentId.Should().Be(childA.Id);
        _ = visited[3].Node.Should().BeSameAs(childB);
        _ = visited[3].ParentId.Should().Be(root.Id);
    }

    [TestMethod]
    public async Task TraverseDepthFirstAsync_VisitsNodesInDepthFirstOrderWithCorrectParentIds()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        root.AddChild(child);

        var visited = new List<(SceneNode Node, NodeId? ParentId)>();
        await SceneTraversal.TraverseDepthFirstAsync(root, (node, parentId) =>
        {
            visited.Add((node, parentId));
            return Task.CompletedTask;
        }).ConfigureAwait(false);

        _ = visited.Should().HaveCount(2);
        _ = visited[0].Node.Should().BeSameAs(root);
        _ = visited[0].ParentId.Should().BeNull();
        _ = visited[1].Node.Should().BeSameAs(child);
        _ = visited[1].ParentId.Should().Be(root.Id);
    }

    [TestMethod]
    public void CollectNodes_And_CollectNodeIds_ReturnAllNodesInDepthFirstOrder()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };
        root.AddChild(child);
        child.AddChild(grandChild);

        var nodes = SceneTraversal.CollectNodes(root);
        var ids = SceneTraversal.CollectNodeIds(root);

        _ = nodes.Should().Equal([root, child, grandChild]);
        _ = ids.Should().Equal([root.Id, child.Id, grandChild.Id]);
    }

    [TestMethod]
    public void FindNodeById_WhenNodeExists_ReturnsNode()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        root.AddChild(child);

        _ = SceneTraversal.FindNodeById(root, root.Id).Should().BeSameAs(root);
        _ = SceneTraversal.FindNodeById(root, child.Id).Should().BeSameAs(child);
    }

    [TestMethod]
    public void FindNodeById_WhenNodeDoesNotExist_ReturnsNull()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };

        _ = SceneTraversal.FindNodeById(root, NodeId.New()).Should().BeNull();
    }

    [TestMethod]
    public void TraversalMethods_WhenGivenNullRoot_ThrowArgumentNullException()
    {
        Action act1 = () => SceneTraversal.TraverseDepthFirst(null!, (_, _) => { });
        Action act2 = () => SceneTraversal.CollectNodes(null!);
        Action act3 = () => SceneTraversal.CollectNodeIds(null!);
        Action act4 = () => SceneTraversal.FindNodeById(null!, NodeId.New());

        _ = act1.Should().Throw<ArgumentNullException>();
        _ = act2.Should().Throw<ArgumentNullException>();
        _ = act3.Should().Throw<ArgumentNullException>();
        _ = act4.Should().Throw<ArgumentNullException>();
    }
}
