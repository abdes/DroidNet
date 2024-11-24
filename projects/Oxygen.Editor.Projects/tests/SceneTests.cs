// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using FluentAssertions;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public class SceneTests
{
    private readonly JsonSerializerOptions jsonOptions = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneTests"/> class.
    /// </summary>
    public SceneTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new SceneJsonConverter(this.ExampleProject));
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
        _ = json.Should().Contain("\"Name\":\"Scene Name\"");
        _ = json.Should().Contain("\"Entities\"");
        _ = json.Should().Contain("\"Name\":\"Entity 1\"");
        _ = json.Should().Contain("\"Name\":\"Entity 2\"");
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
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.Entities.Should().HaveCount(2);
        _ = scene.Entities[0].Name.Should().Be("Entity 1");
        _ = scene.Entities[1].Name.Should().Be("Entity 2");
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
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.Entities.Should().BeEmpty();
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
        _ = act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(Scene.Name)}*");
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
        _ = json.Should().Contain("\"Name\": \"Scene Name\"");
        _ = json.Should().Contain("\"Entities\"");
        _ = json.Should().Contain("\"Name\": \"Entity 1\"");
        _ = json.Should().Contain("\"Name\": \"Entity 2\"");
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
        _ = scene.Should().NotBeNull();
        Debug.Assert(scene is not null, "scene != null");
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.Entities.Should().HaveCount(2);
        _ = scene.Entities[0].Name.Should().Be("Entity 1");
        _ = scene.Entities[1].Name.Should().Be("Entity 2");
    }
}
