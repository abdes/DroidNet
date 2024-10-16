// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Tests;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public class SceneTests
{
    private readonly JsonSerializerOptions jsonOptions = new();

    public SceneTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new Scene.SceneConverter(this.ExampleProject));
    }

    private Project ExampleProject { get; } = new(null!) { Name = "Example Project" };

    [TestMethod]
    public void Should_Serialize_Scene_ToJson()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        scene.Entities.Add(new GameEntity(scene) { Name = "Entity 1" });
        scene.Entities.Add(new GameEntity(scene) { Name = "Entity 2" });

        // Act
        var json = JsonSerializer.Serialize(scene, this.jsonOptions);

        // Assert
        json.Should().Contain("\"Name\":\"Scene Name\"");
        json.Should().Contain("\"Entities\"");
        json.Should().Contain("\"Name\":\"Entity 1\"");
        json.Should().Contain("\"Name\":\"Entity 2\"");
        json.Should().NotContain("\"Project\"");
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
               "Entities":[
                   {
                       "Name":"Entity 1"
                   },
                   {
                       "Name":"Entity 2"
                   }
               ]
            }
            """;

        // Act
        var scene = JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        scene.Name.Should().Be("Scene Name");
        scene.Project.Should().BeSameAs(this.ExampleProject);
        scene.Entities.Should().HaveCount(2);
        scene.Entities[0].Name.Should().Be("Entity 1");
        scene.Entities[1].Name.Should().Be("Entity 2");
    }

    [TestMethod]
    public void Should_Handle_Empty_Entities()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
                "Name":"Scene Name",
                "Entities":[]
            }
            """;

        // Act
        var scene = JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        scene.Name.Should().Be("Scene Name");
        scene.Project.Should().BeSameAs(this.ExampleProject);
        scene.Entities.Should().BeEmpty();
    }

    [TestMethod]
    public void Should_Throw_When_Name_Is_Missing()
    {
        // Arrange
        const string json =
            /*lang=json,strict*/
            """
            {
                "Entities":[
                    {
                        "Name":"Entity 1"
                    },
                    {
                        "Name":"Entity 2"
                    }
                ]
            }
            """;

        // Act
        var act = () => JsonSerializer.Deserialize<Scene>(json, this.jsonOptions);

        // Assert
        act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(Scene.Name)}*");
    }

    [TestMethod]
    public void ToJson_Should_Serialize_Scene()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        scene.Entities.Add(new GameEntity(scene) { Name = "Entity 1" });
        scene.Entities.Add(new GameEntity(scene) { Name = "Entity 2" });

        // Act
        var json = Scene.ToJson(scene);

        // Assert
        json.Should().Contain("\"Name\": \"Scene Name\"");
        json.Should().Contain("\"Entities\"");
        json.Should().Contain("\"Name\": \"Entity 1\"");
        json.Should().Contain("\"Name\": \"Entity 2\"");
        json.Should().NotContain("\"Project\"");
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
               "Entities":[
                   {
                       "Name":"Entity 1"
                   },
                   {
                       "Name":"Entity 2"
                   }
               ]
            }
            """;

        // Act
        var scene = Scene.FromJson(json, this.ExampleProject);

        // Assert
        scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        scene.Name.Should().Be("Scene Name");
        scene.Project.Should().BeSameAs(this.ExampleProject);
        scene.Entities.Should().HaveCount(2);
        scene.Entities[0].Name.Should().Be("Entity 1");
        scene.Entities[1].Name.Should().Be("Entity 2");
    }
}
