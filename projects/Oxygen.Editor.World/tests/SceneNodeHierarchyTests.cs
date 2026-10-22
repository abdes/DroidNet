// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Tests.TestSupport;

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
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");

        parent.Hierarchy.AddChild(child);

        _ = child.Hierarchy.Parent.Should().BeSameAs(parent);
        _ = parent.Hierarchy.Children.Should().Contain(child);
    }

    [TestMethod]
    public void RemoveChild_Detaches_Child()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");

        parent.Hierarchy.AddChild(child);
        parent.Hierarchy.RemoveChild(child);

        _ = child.Hierarchy.Parent.Should().BeNull();
        _ = parent.Hierarchy.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void SetParent_Reparents_Node_From_Old_To_New_Parent()
    {
        var scene = new Scene { Name = "Scene" };
        var parent1 = new SceneNode(scene, "Parent1");
        var parent2 = new SceneNode(scene, "Parent2");
        var child = new SceneNode(scene, "Child");

        parent1.Hierarchy.AddChild(child);
        parent2.Hierarchy.AddChild(child); // Should move from parent1 to parent2

        _ = child.Hierarchy.Parent.Should().BeSameAs(parent2);
        _ = parent1.Hierarchy.Children.Should().NotContain(child);
        _ = parent2.Hierarchy.Children.Should().Contain(child);
    }

    [TestMethod]
    public void SetParent_JoinsNewParentBeforeLeavingOldOne_CollectionChangedObserverSeesChildInBothParents()
    {
        var scene = new Scene { Name = "Scene" };
        var parent1 = new SceneNode(scene, "Parent1");
        var parent2 = new SceneNode(scene, "Parent2");
        var child = new SceneNode(scene, "Child");
        parent1.Hierarchy.AddChild(child);

        var sawAddWhileStillInOldParent = false;
        parent2.Hierarchy.Children.CollectionChanged += (_, args) =>
        {
            if (args.Action == NotifyCollectionChangedAction.Add && args.NewItems?.Contains(child) == true)
            {
                sawAddWhileStillInOldParent = parent1.Hierarchy.Children.Contains(child);
            }
        };

        child.Hierarchy.SetParent(parent2);

        _ = sawAddWhileStillInOldParent.Should().BeTrue();
        _ = child.Hierarchy.Parent.Should().BeSameAs(parent2);
        _ = parent1.Hierarchy.Children.Should().NotContain(child);
        _ = parent2.Hierarchy.Children.Should().Contain(child);
    }

    [TestMethod]
    public void SetParent_WithCurrentParent_IsNoOp()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");
        parent.Hierarchy.AddChild(child);

        var collectionChangedCount = 0;
        parent.Hierarchy.Children.CollectionChanged += (_, _) => collectionChangedCount++;

        child.Hierarchy.SetParent(parent);

        _ = collectionChangedCount.Should().Be(0);
        _ = child.Hierarchy.Parent.Should().BeSameAs(parent);
    }

    [TestMethod]
    public void SetParent_WithNull_DetachesNode()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");
        parent.Hierarchy.AddChild(child);

        child.Hierarchy.SetParent(newParent: null);

        _ = child.Hierarchy.Parent.Should().BeNull();
        _ = parent.Hierarchy.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void SetParent_Direct_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");

        var act = () => node.Hierarchy.AddChild(node);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void SetParent_Indirect_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");
        var grandChild = new SceneNode(scene, "GrandChild");

        parent.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grandChild);

        var act = () => grandChild.Hierarchy.AddChild(parent);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void SetParent_Deep_Circular_Reference_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var a = new SceneNode(scene, "A");
        var b = new SceneNode(scene, "B");
        var c = new SceneNode(scene, "C");
        var d = new SceneNode(scene, "D");

        a.Hierarchy.AddChild(b);
        b.Hierarchy.AddChild(c);
        c.Hierarchy.AddChild(d);

        var act = () => a.Hierarchy.SetParent(d);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void SetParent_ToDescendant_Throws()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grandChild = new SceneNode(scene, "GrandChild");
        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grandChild);

        var act = () => root.Hierarchy.SetParent(grandChild);

        _ = act.Should().Throw<InvalidOperationException>();
        _ = root.Hierarchy.Parent.Should().BeNull();
    }

    [TestMethod]
    public async Task SceneSerializerRoundTrip_KeepsChildOrderAndParentLinks()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var first = new SceneNode(scene, "First");
        var second = new SceneNode(scene, "Second");
        var third = new SceneNode(scene, "Third");
        var nested = new SceneNode(scene, "Nested");
        root.Hierarchy.AddChild(first);
        root.Hierarchy.AddChild(second);
        root.Hierarchy.AddChild(third);
        second.Hierarchy.AddChild(nested);
        scene.RootNodes.Add(root);

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        var restoredRoot = restored.RootNodes.Should().ContainSingle().Subject;
        _ = restoredRoot.Hierarchy.Parent.Should().BeNull();
        _ = restoredRoot.Hierarchy.Children.Select(static node => node.Metadata.Name).Should().Equal("First", "Second", "Third");
        _ = restoredRoot.Hierarchy.Children.Should().OnlyContain(node => node.Hierarchy.Parent == restoredRoot);
        var restoredSecond = restoredRoot.Hierarchy.Children[1];
        var restoredNested = restoredSecond.Hierarchy.Children.Should().ContainSingle().Subject;
        _ = restoredNested.Metadata.Name.Should().Be("Nested");
        _ = restoredNested.Hierarchy.Parent.Should().BeSameAs(restoredSecond);
    }

    [TestMethod]
    public void Descendants_Returns_All_Descendants_In_Tree()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child1 = new SceneNode(scene, "Child1");
        var child2 = new SceneNode(scene, "Child2");
        var grandChild = new SceneNode(scene, "GrandChild");

        root.Hierarchy.AddChild(child1);
        root.Hierarchy.AddChild(child2);
        child1.Hierarchy.AddChild(grandChild);

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
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grandChild = new SceneNode(scene, "GrandChild");

        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grandChild);

        var ancestors = grandChild.Ancestors().ToList();

        _ = ancestors.Should().HaveCount(2);
        _ = ancestors[0].Should().BeSameAs(child);
        _ = ancestors[1].Should().BeSameAs(root);
    }
}
