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
    private readonly JsonSerializerOptions jsonOptions = new();
    private readonly Mock<IProject> projectMock = new();

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneTests" /> class.
    /// </summary>
    public SceneTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new SceneJsonConverter(this.ExampleProject));
    }

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void Should_Serialize_Scene_ToJson()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        using var node1 = new SceneNode(scene) { Name = "Node 1" };
        using var node2 = new SceneNode(scene) { Name = "Node 2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var json = JsonSerializer.Serialize(scene, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"Name\":\"Scene Name\"");
        _ = json.Should().Contain("\"RootNodes\"");
        _ = json.Should().Contain("\"Name\":\"Node 1\"");
        _ = json.Should().Contain("\"Name\":\"Node 2\"");
        _ = json.Should().NotContain("\"Project\"");
    }

    [TestMethod]
    public void Should_Deserialize_Scene_FromJson()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
               "Name":"Scene Name",
               "RootNodes":[
                   {
                       "Name":"Node 1"
                   },
                   {
                       "Name":"Node 2"
                   }
               ]
            }
            """;

        // Act
        var scene = JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.RootNodes.ElementAt(0).Name.Should().Be("Node 1");
        _ = scene.RootNodes.ElementAt(1).Name.Should().Be("Node 2");
    }

    [TestMethod]
    public void Should_Handle_Empty_Nodes()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
                "Name":"Scene Name",
                "RootNodes":[]
            }
            """;

        // Act
        var scene = JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
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
                "RootNodes":[
                    {
                        "Name":"Node 1"
                    },
                    {
                        "Name":"Node 2"
                    }
                ]
            }
            """;

        // Act
        var act = () => JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        _ = act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(Scene.Name)}*");
    }

    [TestMethod]
    public void ToJson_Should_Serialize_Scene()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        using var node1 = new SceneNode(scene) { Name = "Node 1" };
        using var node2 = new SceneNode(scene) { Name = "Node 2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var json = Scene.ToJson(scene);

        // Assert
        _ = json.Should().Contain("\"Name\": \"Scene Name\"");
        _ = json.Should().Contain("\"RootNodes\"");
        _ = json.Should().Contain("\"Name\": \"Node 1\"");
        _ = json.Should().Contain("\"Name\": \"Node 2\"");
        _ = json.Should().NotContain("\"Project\"");
    }

    [TestMethod]
    public void FromJson_Should_Deserialize_Scene()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
               "Name":"Scene Name",
               "RootNodes":[
                   {
                       "Name":"Node 1"
                   },
                   {
                       "Name":"Node 2"
                   }
               ]
            }
            """;

        // Act
        var scene = Scene.FromJson(json, this.ExampleProject);

        // Assert
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.RootNodes.ElementAt(0).Name.Should().Be("Node 1");
        _ = scene.RootNodes.ElementAt(1).Name.Should().Be("Node 2");
    }

    [TestMethod]
    public void Should_RoundTrip_Scene_Serialization()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "RoundTrip Scene" };
        using var node1 = new SceneNode(scene) { Name = "Node A" };
        using var node2 = new SceneNode(scene) { Name = "Node B" };
        node1.Components.Add(new GameComponent(node1) { Name = "GC1" });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var json = Scene.ToJson(scene);
        var deserialized = Scene.FromJson(json, this.ExampleProject);

        // Assert
        _ = deserialized.Should().NotBeNull();
        Debug.Assert(deserialized is not null, "deserialized != null");
        _ = deserialized!.Name.Should().Be(scene.Name);
        _ = deserialized.Project.Should().BeSameAs(this.ExampleProject);
        _ = deserialized.RootNodes.Should().HaveCount(2);
        _ = deserialized.RootNodes.ElementAt(0).Name.Should().Be("Node A");
        _ = deserialized.RootNodes.ElementAt(1).Name.Should().Be("Node B");

        // Nodes should reference their parent scene
        foreach (var n in deserialized.RootNodes)
        {
            _ = n.Scene.Should().BeSameAs(deserialized);
        }
    }

    [TestMethod]
    public void Should_Set_Node_Scene_On_Deserialize()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
               "Name":"Scene Name",
               "RootNodes":[
                   { "Name":"Node 1" },
                   { "Name":"Node 2" }
               ]
            }
            """;

        // Act
        var scene = JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        foreach (var node in scene!.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }
}
