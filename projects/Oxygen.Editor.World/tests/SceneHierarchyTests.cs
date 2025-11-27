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

        _ = child.Parent.Should().BeSameAs(parent);
        _ = parent.Children.Should().Contain(child);
    }

    [TestMethod]
    public void Should_Remove_Child()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var parent = new SceneNode(scene) { Name = "Parent" };
        using var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);
        parent.RemoveChild(child);

        _ = child.Parent.Should().BeNull();
        _ = parent.Children.Should().NotContain(child);
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

        _ = child.Parent.Should().BeSameAs(parent2);
        _ = parent1.Children.Should().NotContain(child);
        _ = parent2.Children.Should().Contain(child);
    }

    [TestMethod]
    public void Should_Prevent_Circular_Reference_Direct()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        using var node = new SceneNode(scene) { Name = "Node" };

        var act = () => node.AddChild(node);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
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

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
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

        _ = descendants.Should().HaveCount(3);
        _ = descendants.Should().Contain(child1);
        _ = descendants.Should().Contain(child2);
        _ = descendants.Should().Contain(grandChild);
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

        _ = ancestors.Should().HaveCount(2);
        _ = ancestors[0].Should().BeSameAs(child);
        _ = ancestors[1].Should().BeSameAs(root);
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
        _ = json.Should().Contain("RootNodes");
        _ = json.Should().Contain("Children");
        _ = json.Should().Contain("Root");
        _ = json.Should().Contain("Child");
    }

    [TestMethod]
    public void Should_Deserialize_Nested_Hierarchy()
    {
        const string json = """
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

        _ = scene.Should().NotBeNull();
        _ = scene!.RootNodes.Should().ContainSingle();

        var root = scene.RootNodes[0];
        _ = root.Name.Should().Be("Root");
        _ = root.Children.Should().ContainSingle();

        var child = root.Children[0];
        _ = child.Name.Should().Be("Child");
        _ = child.Parent.Should().BeSameAs(root);
        _ = child.Scene.Should().BeSameAs(scene);
    }
}
