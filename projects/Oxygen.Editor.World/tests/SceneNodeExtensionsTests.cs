// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class SceneNodeExtensionsTests
{
    private readonly Mock<IProject> projectMock = new();

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void DescendantsAndSelf_Includes_Node_And_All_Descendants()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grand = new SceneNode(scene) { Name = "Grand" };

        root.AddChild(child);
        child.AddChild(grand);

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
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grand = new SceneNode(scene) { Name = "Grand" };

        root.AddChild(child);
        child.AddChild(grand);

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
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grand = new SceneNode(scene) { Name = "Grand" };

        root.AddChild(child);
        child.AddChild(grand);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/Child/Grand");

        _ = found.Should().BeSameAs(grand);
    }

    [TestMethod]
    public void FindByPath_SingleLevelWildcard_Finds_First_Matching()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child1 = new SceneNode(scene) { Name = "ChildA" };
        var child2 = new SceneNode(scene) { Name = "ChildB" };

        root.AddChild(child1);
        root.AddChild(child2);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/*");

        // should find the first child (ChildA)
        _ = found.Should().BeSameAs(child1);
    }

    [TestMethod]
    public void FindByPath_RecursiveWildcard_Finds_Deep_Node()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var a = new SceneNode(scene) { Name = "A" };
        var b = new SceneNode(scene) { Name = "B" };
        var c = new SceneNode(scene) { Name = "C" };

        root.AddChild(a);
        a.AddChild(b);
        b.AddChild(c);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/**/C");

        _ = found.Should().BeSameAs(c);
    }

    [TestMethod]
    public void FindByPath_NullPath_ThrowsArgumentNull()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };

        var act = () => scene.FindByPath(null!);

        _ = act.Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void FindByPath_EmptyPath_ReturnsNull()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        scene.RootNodes.Add(root);

        var found = scene.FindByPath(string.Empty);

        _ = found.Should().BeNull();
    }

    [TestMethod]
    public void FindByPath_LeadingTrailingAndDuplicateSeparators_AreHandled()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };

        root.AddChild(child);
        scene.RootNodes.Add(root);

        var found1 = scene.FindByPath("/Root/Child/");
        var found2 = scene.FindByPath("Root//Child");

        _ = found1.Should().BeSameAs(child);
        _ = found2.Should().BeSameAs(child);
    }

    [TestMethod]
    public void FindByPath_MultipleRoots_FirstMatchIsReturned()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var r1 = new SceneNode(scene) { Name = "R1" };
        var r2 = new SceneNode(scene) { Name = "R2" };
        var a = new SceneNode(scene) { Name = "X" };
        var b = new SceneNode(scene) { Name = "X" };

        r1.AddChild(a);
        r2.AddChild(b);

        scene.RootNodes.Add(r1);
        scene.RootNodes.Add(r2);

        var found = scene.FindByPath("*/X");

        _ = found.Should().BeSameAs(a);
    }

    [TestMethod]
    public void FindByPath_DuplicateNames_Returns_FirstMatchingNode()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var s1 = new SceneNode(scene) { Name = "Same" };
        var s2 = new SceneNode(scene) { Name = "Same" };

        root.AddChild(s1);
        root.AddChild(s2);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("Root/Same");

        _ = found.Should().BeSameAs(s1);
    }

    [TestMethod]
    public void FindByPath_CaseInsensitive_MatchesRegardlessOfCase()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grand = new SceneNode(scene) { Name = "Grand" };

        root.AddChild(child);
        child.AddChild(grand);
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("root/child/grand");

        _ = found.Should().BeSameAs(grand);
    }

    [TestMethod]
    public void FindByPath_NoMatch_ReturnsNull()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        scene.RootNodes.Add(root);

        var found = scene.FindByPath("DoesNotExist");

        _ = found.Should().BeNull();
    }

    [TestMethod]
    public void FindByPath_OnlyRecursiveWildcard_ReturnsFirstNode()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var r = new SceneNode(scene) { Name = "Root" };
        var c = new SceneNode(scene) { Name = "Child" };
        r.AddChild(c);
        scene.RootNodes.Add(r);

        var found = scene.FindByPath("**");

        // '**' should match any path, so first node (Root) is returned
        _ = found.Should().BeSameAs(r);
    }
}
