// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public class SceneTests
{
    private readonly Mock<IProject> projectMock = new();

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void Should_Serialize_Scene_ToJson()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        var node1 = new SceneNode(scene) { Name = "Node 1" };
        var node2 = new SceneNode(scene) { Name = "Node 2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act: serialize the DTO produced by Dehydrate using source-generated context
        var dto = scene.Dehydrate();
        var json = JsonSerializer.Serialize(dto, Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);

        // Assert (PascalCase JSON produced by source-generated context)
        _ = json.Should().Contain("\"Name\": \"Scene Name\"");
        _ = json.Should().Contain("\"RootNodes\"");
        _ = json.Should().Contain("\"Name\": \"Node 1\"");
        _ = json.Should().Contain("\"Name\": \"Node 2\"");
    }

    [TestMethod]
    public void Should_Deserialize_Scene_FromJson()
    {
        // Arrange
        // Act: build DTO in code then hydrate into domain Scene (more robust than relying on string literals)
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000000"),
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000001"),
                    Name = "Node 1",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000002"),
                    Name = "Node 2",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.RootNodes[0].Name.Should().Be("Node 1");
        _ = scene.RootNodes[1].Name.Should().Be("Node 2");
    }

    [TestMethod]
    public void Should_Handle_Empty_Nodes()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
                "Id": "00000000-0000-0000-0000-000000000010",
                "Name":"Scene Name",
                "RootNodes":[]
            }
            """;

        // Act
        var data = JsonSerializer.Deserialize(json, Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);

        _ = data.Should().NotBeNull();
        Debug.Assert(data is not null, "data != null");

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public void Should_Throw_When_Name_Is_Missing()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
                {
                    "Id": "00000000-0000-0000-0000-000000000020",
                    "RootNodes":[
                        {
                            "Id": "00000000-0000-0000-0000-000000000021",
                            "Name":"Node 1",
                            "Components": [ { "$type": "Transform", "Transform": {} } ]
                        },
                        {
                            "Id": "00000000-0000-0000-0000-000000000022",
                            "Name":"Node 2",
                            "Components": [ { "$type": "Transform", "Transform": {} } ]
                        }
                    ]
                }
            """;

        // Act: deserializing to DTO should throw if required property (Name) is missing on the Scene
        var act = () => JsonSerializer.Deserialize(json, Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);

        // Assert
        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void Should_RoundTrip_Scene_Serialization()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "RoundTrip Scene" };
        var node1 = new SceneNode(scene) { Name = "Node A" };
        var node2 = new SceneNode(scene) { Name = "Node B" };

        // avoid adding abstract GameComponent; keep default transform only
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act: serialize DTO then deserialize DTO and hydrate a new Scene
        var json = JsonSerializer.Serialize(scene.Dehydrate(), Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);
        var data = JsonSerializer.Deserialize(json, Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);

        _ = data.Should().NotBeNull();
        Debug.Assert(data is not null, "data != null");

        var deserialized = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        deserialized.Hydrate(data);

        // Assert
        _ = deserialized.Should().NotBeNull();
        Debug.Assert(deserialized is not null, "deserialized != null");
        _ = deserialized!.Name.Should().Be(scene.Name);
        _ = deserialized.Project.Should().BeSameAs(this.ExampleProject);
        _ = deserialized.RootNodes.Should().HaveCount(2);
        _ = deserialized.RootNodes[0].Name.Should().Be("Node A");
        _ = deserialized.RootNodes[1].Name.Should().Be("Node B");

        // Nodes should reference their parent scene
        foreach (var n in deserialized.RootNodes)
        {
            _ = n.Scene.Should().BeSameAs(deserialized);
        }
    }

    [TestMethod]
    public void Should_Set_Node_Scene_On_Deserialize()
    {
        // Arrange: construct DTO programmatically (avoid JSON literal fragility)
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000040"),
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000041"),
                    Name = "Node 1",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000042"),
                    Name = "Node 2",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        foreach (var node in scene!.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }

    [TestMethod]
    public void Should_Add_Child_And_Set_Parent()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);

        _ = child.Parent.Should().BeSameAs(parent);
        _ = parent.Children.Should().Contain(child);
    }

    [TestMethod]
    public void Should_Remove_Child()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);
        parent.RemoveChild(child);

        _ = child.Parent.Should().BeNull();
        _ = parent.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void Should_Reparent_Node()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
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
    public void Should_Prevent_Circular_Reference_Direct()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };

        var act = () => node.AddChild(node);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void Should_Prevent_Circular_Reference_Indirect()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        parent.AddChild(child);
        child.AddChild(grandChild);

        var act = () => grandChild.AddChild(parent);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void Should_Enumerate_Descendants()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
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
    public void Should_Enumerate_Ancestors()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
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

    [TestMethod]
    public void Should_Serialize_Nested_Hierarchy()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };

        root.AddChild(child);
        scene.RootNodes.Add(root);

        var json = JsonSerializer.Serialize(scene.Dehydrate(), Oxygen.Editor.World.Serialization.SceneJsonContext.Default.SceneData);

        // Verify structure roughly (PascalCase)
        _ = json.Should().Contain("RootNodes");
        _ = json.Should().Contain("Children");
        _ = json.Should().Contain("Root");
        _ = json.Should().Contain("Child");
    }

    [TestMethod]
    public void Should_Deserialize_Nested_Hierarchy()
    {
        // Arrange: construct nested DTO programmatically
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000050"),
            Name = "Scene",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000051"),
                    Name = "Root",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                    Children =
                    [
                        new()
                        {
                            Id = Guid.Parse("00000000-0000-0000-0000-000000000052"),
                            Name = "Child",
                            Components =
                            [
                                new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                            ],
                        },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

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
