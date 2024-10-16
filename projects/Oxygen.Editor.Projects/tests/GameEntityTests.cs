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
[TestCategory($"{nameof(GameEntity)}.Json")]
public class GameEntityTests
{
    private readonly JsonSerializerOptions jsonOptions = new();

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

        // Act
        var json = JsonSerializer.Serialize(gameEntity, this.jsonOptions);

        // Assert
        json.Should().Contain("\"Name\":\"Entity Name\"");
        json.Should().NotContain("\"Scene\"");
    }

    [TestMethod]
    public void Should_Deserialize_GameEntity()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Entity Name"
            }
            """;

        // Act
        var gameEntity = JsonSerializer.Deserialize<GameEntity>(json, this.jsonOptions);

        // Assert
        gameEntity.Should().NotBeNull();
        Debug.Assert(gameEntity != null, nameof(gameEntity) + " != null");
        gameEntity.Name.Should().Be("Entity Name");
        gameEntity.Scene.Should().BeSameAs(this.ExampleScene);
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
        act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(GameEntity.Name)}*");
    }

    [TestMethod]
    public void ToJson_Should_Serialize_GameEntity()
    {
        // Arrange
        var gameEntity = new GameEntity(this.ExampleScene) { Name = "Entity Name" };

        // Act
        var json = GameEntity.ToJson(gameEntity);

        // Assert
        json.Should().Contain("\"Name\": \"Entity Name\"");
        json.Should().NotContain("\"Scene\"");
    }

    [TestMethod]
    public void FromJson_Should_Deserialize_GameEntity()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Entity Name"
            }
            """;

        // Act
        var gameEntity = GameEntity.FromJson(json, this.ExampleScene);

        // Assert
        gameEntity.Should().NotBeNull();
        Debug.Assert(gameEntity != null, nameof(gameEntity) + " != null");
        gameEntity.Name.Should().Be("Entity Name");
        gameEntity.Scene.Should().BeSameAs(this.ExampleScene);
    }
}
