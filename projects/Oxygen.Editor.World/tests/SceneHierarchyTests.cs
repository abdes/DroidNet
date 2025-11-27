// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Moq;

namespace Oxygen.Editor.World.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class SceneHierarchyTests
{
    private readonly Mock<IProject> projectMock = new();

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void Should_Add_Child_And_Set_Parent()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var parent = new SceneNode(scene) { Name = "Parent" };
        using var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);

        child.Parent.Should().BeSameAs(parent);
        parent.Children.Should().Contain(child);
    }

    [TestMethod]
    public void Should_Remove_Child()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var parent = new SceneNode(scene) { Name = "Parent" };
        using var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);
        parent.RemoveChild(child);

        child.Parent.Should().BeNull();
        parent.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void Should_Reparent_Node()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var parent1 = new SceneNode(scene) { Name = "Parent1" };
        using var parent2 = new SceneNode(scene) { Name = "Parent2" };
        using var child = new SceneNode(scene) { Name = "Child" };

        parent1.AddChild(child);
        parent2.AddChild(child); // Should move from parent1 to parent2

        child.Parent.Should().BeSameAs(parent2);
        parent1.Children.Should().NotContain(child);
        parent2.Children.Should().Contain(child);
    }

    [TestMethod]
    public void Should_Prevent_Circular_Reference_Direct()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var node = new SceneNode(scene) { Name = "Node" };

        var act = () => node.AddChild(node);

        act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void Should_Prevent_Circular_Reference_Indirect()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var parent = new SceneNode(scene) { Name = "Parent" };
        using var child = new SceneNode(scene) { Name = "Child" };
        using var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        parent.AddChild(child);
        child.AddChild(grandChild);

        var act = () => grandChild.AddChild(parent);

        act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void Should_Enumerate_Descendants()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var root = new SceneNode(scene) { Name = "Root" };
        using var child1 = new SceneNode(scene) { Name = "Child1" };
        using var child2 = new SceneNode(scene) { Name = "Child2" };
        using var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child1);
        root.AddChild(child2);
        child1.AddChild(grandChild);

        var descendants = root.Descendants().ToList();

        descendants.Should().HaveCount(3);
        descendants.Should().Contain(child1);
        descendants.Should().Contain(child2);
        descendants.Should().Contain(grandChild);
    }

    [TestMethod]
    public void Should_Enumerate_Ancestors()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var root = new SceneNode(scene) { Name = "Root" };
        using var child = new SceneNode(scene) { Name = "Child" };
        using var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child);
        child.AddChild(grandChild);

        var ancestors = grandChild.Ancestors().ToList();

        ancestors.Should().HaveCount(2);
        ancestors.ElementAt(0).Should().BeSameAs(child);
        ancestors.ElementAt(1).Should().BeSameAs(root);
    }

    [TestMethod]
    public void Should_Serialize_Nested_Hierarchy()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var root = new SceneNode(scene) { Name = "Root" };
        using var child = new SceneNode(scene) { Name = "Child" };

        root.AddChild(child);
        scene.RootNodes.Add(root);

        var json = Scene.ToJson(scene);

        // Verify structure roughly
        json.Should().Contain("RootNodes");
        json.Should().Contain("Children");
        json.Should().Contain("Root");
        json.Should().Contain("Child");
    }

    [TestMethod]
    public void Should_Deserialize_Nested_Hierarchy()
    {
        var json = """
        {
            "Name": "Scene",
            "RootNodes": [
                {
                    "Name": "Root",
                    "Children": [
                        {
                            "Name": "Child"
                        }
                    ]
                }
            ]
        }
        """;

        var scene = Scene.FromJson(json, this.ExampleProject);

        scene.Should().NotBeNull();
        scene!.RootNodes.Should().HaveCount(1);

        var root = scene.RootNodes.First();
        root.Name.Should().Be("Root");
        root.Children.Should().HaveCount(1);

        var child = root.Children.First();
        child.Name.Should().Be("Child");
        child.Parent.Should().BeSameAs(root);
        child.Scene.Should().BeSameAs(scene);
    }
}
