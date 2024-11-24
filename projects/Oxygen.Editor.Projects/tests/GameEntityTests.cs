// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using FluentAssertions;

namespace Oxygen.Editor.Projects.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(GameEntity)}.Json")]
public class GameEntityTests
{
    private readonly JsonSerializerOptions jsonOptions = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="GameEntityTests"/> class.
    /// </summary>
    public GameEntityTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new GameEntity.GameEntityConverter(this.ExampleScene));
    }

    private Scene ExampleScene { get; } = new(null!) { Name = "Example Scene" };

    [TestMethod]
    public void Should_Serialize_GameEntity()
    {
        // Arrange
        var gameEntity = new GameEntity(this.ExampleScene) { Name = "Entity Name" };
        gameEntity.Components.Add(new GameComponent(gameEntity) { Name = "Component 1" });
        gameEntity.Components.Add(new GameComponent(gameEntity) { Name = "Component 2" });

        // Act
        var json = JsonSerializer.Serialize(gameEntity, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"Name\":\"Entity Name\"");
        _ = json.Should().Contain("\"Components\"");
        _ = json.Should().Contain("\"Name\":\"Component 1\"");
        _ = json.Should().Contain("\"Name\":\"Component 2\"");
        _ = json.Should().NotContain("\"Scene\"");
    }

    [TestMethod]
    public void Should_Deserialize_GameEntity()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Entity Name",
                "Components":[
                    {
                        "Name":"Component 1"
                    },
                    {
                        "Name":"Component 2"
                    }
                ]
            }
            """;

        // Act
        var gameEntity = JsonSerializer.Deserialize<GameEntity>(json, this.jsonOptions);

        // Assert
        _ = gameEntity.Should().NotBeNull();
        Debug.Assert(gameEntity != null, nameof(gameEntity) + " != null");
        _ = gameEntity.Name.Should().Be("Entity Name");
        _ = gameEntity.Scene.Should().BeSameAs(this.ExampleScene);
        _ = gameEntity.Components.Should().HaveCount(2);
        _ = gameEntity.Components[0].Name.Should().Be("Component 1");
        _ = gameEntity.Components[1].Name.Should().Be("Component 2");
    }

    [TestMethod]
    public void Should_ThrowJsonException_WhenDeserialize_MissingRequiredProperty()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
            }
            """;

        // Act
        var act = () => JsonSerializer.Deserialize<GameEntity>(json, this.jsonOptions);

        // Assert
        _ = act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(GameEntity.Name)}*");
    }

    [TestMethod]
    public void ToJson_Should_Serialize_GameEntity()
    {
        // Arrange
        var gameEntity = new GameEntity(this.ExampleScene) { Name = "Entity Name" };
        gameEntity.Components.Add(new GameComponent(gameEntity) { Name = "Component 1" });
        gameEntity.Components.Add(
            new Transform(gameEntity)
            {
                Name = "Component 2",
                Position = new Vector3(1, 2, 3),
            });

        // Act
        var json = GameEntity.ToJson(gameEntity);

        // Assert
        _ = json.Should().Contain("\"Name\": \"Entity Name\"");
        _ = json.Should().Contain("\"Components\"");
        _ = json.Should().Contain("\"Name\": \"Component 1\"");
        _ = json.Should().Contain("\"Name\": \"Component 2\"");
        _ = json.Should().NotContain("\"Scene\"");
    }

    [TestMethod]
    public void FromJson_Should_Deserialize_GameEntity()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
              "Components": [
                {
                  "$type": "Base",
                  "Name": "Component 1"
                },
                {
                  "$type": "Transform",
                  "Position":
                  {
                    "x": 1,
                    "y": 2,
                    "z": 3
                  },
                  "Rotation": {},
                  "Scale": {},
                  "Name": "Component 2"
                }
              ],
              "Name": "Entity Name"
            }
            """;

        // Act
        var gameEntity = GameEntity.FromJson(json, this.ExampleScene);

        // Assert
        _ = gameEntity.Should().NotBeNull();
        Debug.Assert(gameEntity != null, nameof(gameEntity) + " != null");
        _ = gameEntity.Name.Should().Be("Entity Name");
        _ = gameEntity.Scene.Should().BeSameAs(this.ExampleScene);
        _ = gameEntity.Components.Should().HaveCount(2);
        _ = gameEntity.Components[0].Name.Should().Be("Component 1");
        _ = gameEntity.Components[0].Should().BeOfType<GameComponent>();
        _ = gameEntity.Components[1].Name.Should().Be("Component 2");
        _ = gameEntity.Components[1].Should().BeOfType<Transform>();
        _ = gameEntity.Components[1].As<Transform>().Position.Should().BeEquivalentTo(new Vector3(1, 2, 3));
    }
}
