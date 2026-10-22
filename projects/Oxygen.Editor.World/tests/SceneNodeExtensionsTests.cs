// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class SceneNodeExtensionsTests
{
    [TestMethod]
    public void DescendantsAndSelf_Includes_Node_And_All_Descendants()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grand = new SceneNode(scene, "Grand");

        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grand);

        scene.RootNodes.Add(root);

        var list = root.DescendantsAndSelf().ToList();

        _ = list.Should().HaveCount(3);
        _ = list[0].Should().BeSameAs(root);
        _ = list[1].Should().BeSameAs(child);
        _ = list[2].Should().BeSameAs(grand);
    }

    [TestMethod]
    public void AncestorsAndSelf_Includes_Node_Then_Parents()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grand = new SceneNode(scene, "Grand");

        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grand);

        scene.RootNodes.Add(root);

        var list = grand.AncestorsAndSelf().ToList();

        _ = list.Should().HaveCount(3);
        _ = list[0].Should().BeSameAs(grand);
        _ = list[1].Should().BeSameAs(child);
        _ = list[2].Should().BeSameAs(root);
    }

    [TestMethod]
    public void FindByPath_ExactPath_Finds_Node()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grand = new SceneNode(scene, "Grand");

        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grand);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/Child/Grand");

        _ = found.Should().BeSameAs(grand);
    }

    [TestMethod]
    public void FindByPath_SingleLevelWildcard_Finds_First_Matching()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child1 = new SceneNode(scene, "ChildA");
        var child2 = new SceneNode(scene, "ChildB");

        root.Hierarchy.AddChild(child1);
        root.Hierarchy.AddChild(child2);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/*");

        // should find the first child (ChildA)
        _ = found.Should().BeSameAs(child1);
    }

    [TestMethod]
    public void FindByPath_RecursiveWildcard_Finds_Deep_Node()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var a = new SceneNode(scene, "A");
        var b = new SceneNode(scene, "B");
        var c = new SceneNode(scene, "C");

        root.Hierarchy.AddChild(a);
        a.Hierarchy.AddChild(b);
        b.Hierarchy.AddChild(c);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/**/C");

        _ = found.Should().BeSameAs(c);
    }

    [TestMethod]
    public void FindByPath_NullPath_ThrowsArgumentNull()
    {
        var scene = new Scene { Name = "Scene" };

        var act = () => scene.FindByPath(null!);

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void FindByPath_EmptyPath_ReturnsNull()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        scene.RootNodes.Add(root);

        var found = scene.FindByPath(string.Empty);

        _ = found.Should().BeNull();
    }

    [TestMethod]
    public void FindByPath_LeadingTrailingAndDuplicateSeparators_AreHandled()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");

        root.Hierarchy.AddChild(child);
        scene.RootNodes.Add(root);

        var found1 = scene.FindByPath("/Root/Child/");
        var found2 = scene.FindByPath("Root//Child");

        _ = found1.Should().BeSameAs(child);
        _ = found2.Should().BeSameAs(child);
    }

    [TestMethod]
    public void FindByPath_MultipleRoots_FirstMatchIsReturned()
    {
        var scene = new Scene { Name = "Scene" };
        var r1 = new SceneNode(scene, "R1");
        var r2 = new SceneNode(scene, "R2");
        var a = new SceneNode(scene, "X");
        var b = new SceneNode(scene, "X");

        r1.Hierarchy.AddChild(a);
        r2.Hierarchy.AddChild(b);

        scene.RootNodes.Add(r1);
        scene.RootNodes.Add(r2);

        var found = scene.FindByPath("*/X");

        _ = found.Should().BeSameAs(a);
    }

    [TestMethod]
    public void FindByPath_DuplicateNames_Returns_FirstMatchingNode()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var s1 = new SceneNode(scene, "Same");
        var s2 = new SceneNode(scene, "Same");

        root.Hierarchy.AddChild(s1);
        root.Hierarchy.AddChild(s2);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/Same");

        _ = found.Should().BeSameAs(s1);
    }

    [TestMethod]
    public void FindByPath_CaseInsensitive_MatchesRegardlessOfCase()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");
        var grand = new SceneNode(scene, "Grand");

        root.Hierarchy.AddChild(child);
        child.Hierarchy.AddChild(grand);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("root/child/grand");

        _ = found.Should().BeSameAs(grand);
    }

    [TestMethod]
    public void FindByPath_NoMatch_ReturnsNull()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("DoesNotExist");

        _ = found.Should().BeNull();
    }

    [TestMethod]
    public void FindByPath_OnlyRecursiveWildcard_ReturnsFirstNode()
    {
        var scene = new Scene { Name = "Scene" };
        var r = new SceneNode(scene, "Root");
        var c = new SceneNode(scene, "Child");
        r.Hierarchy.AddChild(c);
        scene.RootNodes.Add(r);

        var found = scene.FindByPath("**");

        // '**' should match any path, so first node (Root) is returned
        _ = found.Should().BeSameAs(r);
    }
}
