// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Runtime.Engine.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(SceneNode)}.Json")]
public class SceneNodeTests
{
    private readonly JsonSerializerOptions jsonOptions = new();

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNodeTests" /> class.
    /// </summary>
    public SceneNodeTests()
    {
        this.jsonOptions.WriteIndented = false;
        this.jsonOptions.Converters.Add(new SceneNode.SceneNodeConverter(this.ExampleScene));
    }

    private Scene ExampleScene { get; } = new(null!) { Name = "Example Scene" };

    [TestMethod]
    public void Should_Serialize_SceneNode()
    {
        // Arrange
        using var node = new SceneNode(this.ExampleScene) { Name = "Entity Name" };
        node.Components.Add(new GameComponent(node) { Name = "Component 1" });
        node.Components.Add(new GameComponent(node) { Name = "Component 2" });

        // Act
        var json = JsonSerializer.Serialize(node, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"Name\":\"Entity Name\"");
        _ = json.Should().Contain("\"Components\"");
        _ = json.Should().Contain("\"Name\":\"Component 1\"");
        _ = json.Should().Contain("\"Name\":\"Component 2\"");
        _ = json.Should().NotContain("\"Scene\"");
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
        var act = () => JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = act.Should().Throw<JsonException>().WithMessage($"*deserialization*required*{nameof(SceneNode.Name)}*");
    }

    [TestMethod]
    public void ToJson_Should_Serialize_SceneNode()
    {
        // Arrange
        using var node = new SceneNode(this.ExampleScene) { Name = "Entity Name" };
        node.Components.Add(new GameComponent(node) { Name = "Component 1" });
        node.Components.Add(
            new Transform(node) { Name = "Component 2", Position = new Vector3(1, 2, 3) });

        // Act
        var json = SceneNode.ToJson(node);

        // Assert
        _ = json.Should().Contain("\"Name\": \"Entity Name\"");
        _ = json.Should().Contain("\"Components\"");
        _ = json.Should().Contain("\"Name\": \"Component 1\"");
        _ = json.Should().Contain("\"Name\": \"Component 2\"");
        _ = json.Should().NotContain("\"Scene\"");
    }

    [TestMethod]
    public void FromJson_Should_Deserialize_SceneNode()
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
              "Name": "Node Name"
            }
            """;

        // Act
        var sceneNode = SceneNode.FromJson(json, this.ExampleScene);

        // Assert
        _ = sceneNode.Should().NotBeNull();
        Debug.Assert(sceneNode != null, nameof(sceneNode) + " != null");
        _ = sceneNode.Name.Should().Be("Node Name");
        _ = sceneNode.Scene.Should().BeSameAs(this.ExampleScene);
        _ = sceneNode.Components.Should().HaveCount(2);
        _ = sceneNode.Components.ElementAt(0).Name.Should().Be("Component 1");
        _ = sceneNode.Components.ElementAt(0).Should().BeOfType<GameComponent>();
        _ = sceneNode.Components.ElementAt(1).Name.Should().Be("Component 2");
        _ = sceneNode.Components.ElementAt(1).Should().BeOfType<Transform>();
        _ = sceneNode.Components.ElementAt(1).As<Transform>().Position.Should().BeEquivalentTo(new Vector3(1, 2, 3));
    }

    [TestMethod]
    public void Should_Deserialize_SceneNode_And_EnsureTransform()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Node Name",
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
        var node = JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = node.Should().NotBeNull();
        Debug.Assert(node != null, nameof(node) + " != null");
        _ = node.Name.Should().Be("Node Name");
        _ = node.Scene.Should().BeSameAs(this.ExampleScene);

        // A Transform component is always present, so we expect 3 components total.
        _ = node.Components.Should().HaveCount(3);
        _ = node.Components.ElementAt(0).Name.Should().Be("Component 1");
        _ = node.Components.ElementAt(1).Name.Should().Be("Component 2");

        // The transform should be added automatically as the last component.
        _ = node.Components.ElementAt(2).Should().BeOfType<Transform>();
        _ = node.Components.ElementAt(2).Name.Should().Be("Transform");
    }

    [TestMethod]
    public void Should_RoundTrip_SceneNode_Serialization()
    {
        // Arrange
        using var node = new SceneNode(this.ExampleScene) { Name = "RT Node" };
        node.Components.Add(new GameComponent(node) { Name = "C1" });
        node.Components.Add(new Transform(node) { Name = "T1", Position = new Vector3(4, 5, 6) });

        // Act
        var json = SceneNode.ToJson(node);
        var deserialized = SceneNode.FromJson(json, this.ExampleScene);

        // Assert
        _ = deserialized.Should().NotBeNull();
        Debug.Assert(deserialized != null, "deserialized != null");
        _ = deserialized.Name.Should().Be(node.Name);
        _ = deserialized.Scene.Should().BeSameAs(this.ExampleScene);
        _ = deserialized.Components.Should().HaveCount(3); // original 2 + always-present transform (may be duplicate if ToJson included transform explicitly)
        _ = deserialized.Components.ElementAt(0).Name.Should().Be("C1");
        _ = deserialized.Components.ElementAt(1).Name.Should().Be("T1");

        // Ensure each component's Node is set to the deserialized node
        foreach (var c in deserialized.Components)
        {
            _ = c.Node.Should().BeSameAs(deserialized);
        }
    }

    [TestMethod]
    public void Should_Always_Add_Transform_When_Missing()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Node Name",
                "Components":[
                    { "Name":"Component 1" }
                ]
            }
            """;

        // Act
        var node = JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = node.Should().NotBeNull();
        Debug.Assert(node != null, "node != null");
        _ = node.Components.Should().HaveCount(2); // one provided + injected Transform
        _ = node.Components.ElementAt(1).Should().BeOfType<Transform>();
        _ = node.Components.ElementAt(1).As<Transform>().Position.Should().BeEquivalentTo(new Vector3(0, 0, 0));
    }

    [TestMethod]
    public void Should_Not_Duplicate_Transform_When_ProvidedInJson()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Node Name",
                "Components":[
                    { "Name":"Component 1" },
                    { "$type":"Transform", "Name":"Transform", "Position": { "x":1, "y":2, "z":3 }, "Rotation":{}, "Scale":{} }
                ]
            }
            """;

        // Act
        var node = JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = node.Should().NotBeNull();
        Debug.Assert(node != null, "node != null");

        // Exactly one transform (provided in JSON) plus the named component => count == 2
        _ = node.Components.Should().HaveCount(2);
        _ = node.Components.Count(c => c is Transform).Should().Be(1);
        _ = node.Components.First(c => c is Transform).As<Transform>().Position.Should().BeEquivalentTo(new Vector3(1, 2, 3));
    }

    [TestMethod]
    public void Should_Set_Component_Node_Property_On_Deserialize()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Node Name",
                "Components":[
                    { "Name":"Component A" }
                ]
            }
            """;

        // Act
        var node = JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = node.Should().NotBeNull();
        Debug.Assert(node != null, "node != null");
        foreach (var c in node.Components)
        {
            _ = c.Node.Should().BeSameAs(node);
        }
    }

    [TestMethod]
    public void Should_Handle_Empty_Components_Array_By_Adding_Transform()
    {
        // Arrange
        const string json = /*lang=json,strict*/
            """
            {
                "Name":"Node Name",
                "Components":[]
            }
            """;

        // Act
        var node = JsonSerializer.Deserialize<SceneNode>(json, this.jsonOptions);

        // Assert
        _ = node.Should().NotBeNull();
        Debug.Assert(node != null, "node != null");
        _ = node.Components.Should().HaveCount(1);
        _ = node.Components.ElementAt(0).Should().BeOfType<Transform>();
    }
}
