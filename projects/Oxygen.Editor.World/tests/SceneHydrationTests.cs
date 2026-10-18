// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;
using SceneData = Oxygen.Editor.World.Serialization.SceneData;
using TransformData = Oxygen.Editor.World.Serialization.TransformData;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies <see cref="Scene"/> hydration, dehydration, and light validation safety during state transfers.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Scene))]
public sealed class SceneHydrationTests
{
    [TestMethod]
    public void DuplicateStoredLightAssignmentRejectsHydrationBeforeReplacingScene()
    {
        var scene = new Scene { Name = "Accepted" };
        var retained = new SceneNode(scene) { Name = "Retained" };
        scene.RootNodes.Add(retained);
        var candidate = new SceneData
        {
            Id = SceneId.New(),
            Name = "Invalid lights",
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "Hidden owner", IsVisible = false,
                    Components = [new DirectionalLightData { Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary }],
                },
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "Duplicate",
                    Components = [new DirectionalLightData { Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary }],
                },
            ],
        };
        var hydrate = () => scene.Hydrate(candidate);
        _ = hydrate.Should().Throw<ArgumentException>();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(retained);
    }

    [TestMethod]
    public void Hydrate_ReplacesExistingRootNodes_DoesNotAppend()
    {
        var scene = new Scene { Name = "Scene" };
        var oldNode = new SceneNode(scene) { Name = "Old NodeValue" };
        scene.RootNodes.Add(oldNode);

        var newNodeId = NodeId.New();
        var data = new SceneData
        {
            Id = scene.Id,
            Name = "Hydrated Scene",
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = newNodeId,
                    Name = "New NodeValue",
                    Components = [new TransformData { Name = "Transform" }],
                },
            ],
        };

        scene.Hydrate(data);

        _ = scene.RootNodes.Should().ContainSingle();
        _ = scene.RootNodes[0].Id.Should().Be(newNodeId);
        _ = scene.RootNodes[0].Name.Should().Be("New NodeValue");
        _ = scene.RootNodes.Should().NotContain(oldNode);
    }

    [TestMethod]
    public void Hydrate_RaisesNoChangeNotifications()
    {
        var scene = new Scene { Name = "Scene" };
        var recorder = new PropertyChangeRecorder(scene);

        var data = new SceneData
        {
            Id = scene.Id,
            Name = "Hydrated Scene",
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "NodeValue",
                    Components = [new TransformData { Name = "Transform" }],
                },
            ],
            Environment = new SceneEnvironmentData { AtmosphereEnabled = false },
        };

        scene.Hydrate(data);

        _ = recorder.Changing.Should().BeEmpty();
        _ = recorder.Changed.Should().BeEmpty();
    }

    [TestMethod]
    public void Hydrate_WhenLightsAreInvalid_RejectsBeforeTouchingSceneAndLeavesPreviousContentIntact()
    {
        var scene = new Scene { Name = "Initial Scene" };
        var retained = new SceneNode(scene) { Name = "Retained NodeValue" };
        scene.RootNodes.Add(retained);

        var candidate = new SceneData
        {
            Id = SceneId.New(),
            Name = "Invalid Lights Scene",
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "First Sun",
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Light 1", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Name = "Second Sun",
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Light 2", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
            ],
        };

        var act = () => scene.Hydrate(candidate);

        _ = act.Should().Throw<ArgumentException>();
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(retained);
        _ = scene.Name.Should().Be("Initial Scene");
    }

    [TestMethod]
    public void Dehydrate_WhenInMemoryLightsBreakLightValidation_ThrowsInvalidOperationException()
    {
        var scene = new Scene { Name = "Scene With Invalid Lights" };
        var node1 = new SceneNode(scene) { Name = "Sun 1" };
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun 1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene) { Name = "Sun 2" };
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun 2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        var act = () => scene.Dehydrate();

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Hydrate_Populates_Scene_From_SceneData()
    {
        // Arrange
        // Act: build DTO in code then hydrate into domain Scene (more robust than relying on string literals)
        var data = new SceneData
        {
            Id = SceneId.Empty,
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000001")),
                    Name = "NodeValue 1",
                    Components =
                    [
                        new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                    ],
                },
                new()
                {
                    Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000002")),
                    Name = "NodeValue 2",
                    Components =
                    [
                        new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                    ],
                },
            ],
        };

        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert - hydrate logic sets values and attaches created nodes to the scene
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.RootNodes[0].Name.Should().Be("NodeValue 1");
        _ = scene.RootNodes[1].Name.Should().Be("NodeValue 2");
        foreach (var node in scene.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }

    [TestMethod]
    public void Hydrate_With_Empty_RootNodes_Populates_Empty_RootNodes()
    {
        // Arrange
        // Act: construct DTO equivalent to the JSON literal used previously
        var data = new SceneData { Id = SceneId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000010")), Name = "Scene Name", RootNodes = [] };

        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public void Hydrate_Node_Sets_NodeScene_For_All_RootNodes()
    {
        // Arrange: construct DTO programmatically (avoid JSON literal fragility)
        var data = new SceneData
        {
            Id = SceneId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000040")),
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000041")),
                    Name = "NodeValue 1",
                    Components =
                    [
                        new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                    ],
                },
                new()
                {
                    Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000042")),
                    Name = "NodeValue 2",
                    Components =
                    [
                        new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                    ],
                },
            ],
        };

        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        foreach (var node in scene!.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }

    [TestMethod]
    public void Dehydrate_Then_Hydrate_Restores_SceneContents()
    {
        // Arrange
        var scene = new Scene { Name = "RoundTrip Scene" };
        var node1 = new SceneNode(scene) { Name = "NodeValue A" };
        var node2 = new SceneNode(scene) { Name = "NodeValue B" };

        // avoid adding abstract GameComponent; keep default transform only
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act: Dehydrate -> DTO then hydrate a new Scene using the DTO (unit check; no JSON serializer)
        var dto = scene.Dehydrate();
        var deserialized = new Scene { Name = dto.Name, Id = dto.Id };
        deserialized.Hydrate(dto);

        // Assert basic structural equivalence
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Name.Should().Be(scene.Name);
        _ = deserialized.RootNodes.Should().HaveCount(2);
        _ = deserialized.RootNodes[0].Name.Should().Be("NodeValue A");
        _ = deserialized.RootNodes[1].Name.Should().Be("NodeValue B");

        // Nodes should reference their parent scene
        foreach (var n in deserialized.RootNodes)
        {
            _ = n.Scene.Should().BeSameAs(deserialized);
        }
    }
}
