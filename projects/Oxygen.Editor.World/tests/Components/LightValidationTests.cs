// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies scene light validation rules across in-memory and serialized scenes.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(LightValidation))]
public sealed class LightValidationTests
{
    [TestMethod]
    public void ValidateScene_WhenNodeHasTwoLights_ReturnsErrorOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "MultiLightNode" };
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        _ = node.AddComponent(new PointLightComponent { Name = "Point" });
        scene.RootNodes.Add(node);

        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = node.Id,
                    Name = node.Name,
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Sun" },
                        new PointLightData { Name = "Point" },
                    ],
                },
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_WhenTwoDirectionalLightsClaimSameAtmosphereSlot_ReturnsErrorOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node1 = new SceneNode(scene) { Name = "Sun1" };
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene) { Name = "Sun2" };
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = node1.Id,
                    Name = node1.Name,
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
                new SceneNodeData
                {
                    Id = node2.Id,
                    Name = node2.Name,
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_InactiveNodesWithConflictingLights_StillCountOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node1 = new SceneNode(scene) { Name = "InactiveSun", IsActive = false };
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene) { Name = "ActiveSun", IsActive = true };
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = node1.Id,
                    Name = node1.Name,
                    IsActive = false,
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
                new SceneNodeData
                {
                    Id = node2.Id,
                    Name = node2.Name,
                    IsActive = true,
                    Components =
                    [
                        new TransformData { Name = "Transform" },
                        new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary },
                    ],
                },
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_SceneAndSceneDataOverloads_AgreeOnValidScene()
    {
        var scene = new Scene { Name = "Valid Scene" };
        var node1 = new SceneNode(scene) { Name = "Sun" };
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene) { Name = "Moon" };
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Moon", AtmosphereSlot = AtmosphereLightSlot.Secondary });
        var node3 = new SceneNode(scene) { Name = "Torch" };
        _ = node3.AddComponent(new PointLightComponent { Name = "Torch" });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);
        scene.RootNodes.Add(node3);

        var sceneData = scene.Dehydrate();

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().BeNull();
        _ = dataError.Should().BeNull();
    }
}
