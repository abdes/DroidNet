// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies hierarchy mutation, traversal, cycle rejection, and reparenting notification ordering.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneNode))]
public sealed class SceneNodeHierarchyTests
{
    [TestMethod]
    public void AddChild_Attaches_Child_And_Sets_Parent()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);

        _ = child.Parent.Should().BeSameAs(parent);
        _ = parent.Children.Should().Contain(child);
    }

    [TestMethod]
    public void RemoveChild_Detaches_Child()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);
        parent.RemoveChild(child);

        _ = child.Parent.Should().BeNull();
        _ = parent.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void SetParent_Reparents_Node_From_Old_To_New_Parent()
    {
        var scene = new Scene { Name = "Scene" };
        var parent1 = new SceneNode(scene) { Name = "Parent1" };
        var parent2 = new SceneNode(scene) { Name = "Parent2" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent1.AddChild(child);
        parent2.AddChild(child); // Should move from parent1 to parent2

        _ = child.Parent.Should().BeSameAs(parent2);
        _ = parent1.Children.Should().NotContain(child);
        _ = parent2.Children.Should().Contain(child);
    }

    [TestMethod]
    public void SetParent_JoinsNewParentBeforeLeavingOldOne_CollectionChangedObserverSeesChildInBothParents()
    {
        var scene = new Scene { Name = "Scene" };
        var parent1 = new SceneNode(scene) { Name = "Parent1" };
        var parent2 = new SceneNode(scene) { Name = "Parent2" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent1.AddChild(child);

        var sawAddWhileStillInOldParent = false;
        parent2.Children.CollectionChanged += (_, args) =>
        {
            if (args.Action == NotifyCollectionChangedAction.Add && args.NewItems?.Contains(child) == true)
            {
                sawAddWhileStillInOldParent = parent1.Children.Contains(child);
            }
        };

        child.SetParent(parent2);

        _ = sawAddWhileStillInOldParent.Should().BeTrue();
        _ = child.Parent.Should().BeSameAs(parent2);
        _ = parent1.Children.Should().NotContain(child);
        _ = parent2.Children.Should().Contain(child);
    }

    [TestMethod]
    public void SetParent_WithCurrentParent_IsNoOp()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);

        var collectionChangedCount = 0;
        parent.Children.CollectionChanged += (_, _) => collectionChangedCount++;

        child.SetParent(parent);

        _ = collectionChangedCount.Should().Be(0);
        _ = child.Parent.Should().BeSameAs(parent);
    }

    [TestMethod]
    public void SetParent_WithNull_DetachesNode()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);

        child.SetParent(newParent: null);

        _ = child.Parent.Should().BeNull();
        _ = parent.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void SetParent_Direct_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };

        var act = () => node.AddChild(node);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void SetParent_Indirect_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        parent.AddChild(child);
        child.AddChild(grandChild);

        var act = () => grandChild.AddChild(parent);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void SetParent_Deep_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var a = new SceneNode(scene) { Name = "A" };
        var b = new SceneNode(scene) { Name = "B" };
        var c = new SceneNode(scene) { Name = "C" };
        var d = new SceneNode(scene) { Name = "D" };

        a.AddChild(b);
        b.AddChild(c);
        c.AddChild(d);

        var act = () => a.SetParent(d);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Descendants_Returns_All_Descendants_In_Tree()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child1 = new SceneNode(scene) { Name = "Child1" };
        var child2 = new SceneNode(scene) { Name = "Child2" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child1);
        root.AddChild(child2);
        child1.AddChild(grandChild);

        var descendants = root.Descendants().ToList();

        _ = descendants.Should().HaveCount(3);
        _ = descendants.Should().Contain(child1);
        _ = descendants.Should().Contain(child2);
        _ = descendants.Should().Contain(grandChild);
    }

    [TestMethod]
    public void Ancestors_Returns_Ordered_Ancestors()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child);
        child.AddChild(grandChild);

        var ancestors = grandChild.Ancestors().ToList();

        _ = ancestors.Should().HaveCount(2);
        _ = ancestors[0].Should().BeSameAs(child);
        _ = ancestors[1].Should().BeSameAs(root);
    }
}
