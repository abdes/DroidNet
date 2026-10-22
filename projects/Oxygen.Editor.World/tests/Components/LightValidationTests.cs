// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
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
        var node = new SceneNode(scene, "MultiLightNode");
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        _ = node.AddComponent(new PointLightComponent { Name = "Point" });
        scene.RootNodes.Add(node);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes = [SceneTestSupport.NodeData("MultiLightNode", new DirectionalLightData { Name = "Sun" }, new PointLightData { Name = "Point" })],
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
        var node1 = new SceneNode(scene, "Sun1");
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "Sun2");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                SceneTestSupport.NodeData("Sun1", new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary }),
                SceneTestSupport.NodeData("Sun2", new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary }),
            ],
        };

        var sceneError = LightValidation.ValidateScene(scene);
        var dataError = LightValidation.ValidateScene(sceneData);

        _ = sceneError.Should().NotBeNull();
        _ = dataError.Should().NotBeNull();
    }

    [TestMethod]
    public void ValidateScene_InvisibleNodesWithConflictingLights_StillCountOnBothOverloads()
    {
        var scene = new Scene { Name = "Scene" };
        var node1 = new SceneNode(scene, "InvisibleSun");
        node1.Flags.IsVisible = false;
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "VisibleSun");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary });
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // The record holds what the scene model refuses to write, as a hand-edited file could.
        var sceneData = new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes =
            [
                new SceneNodeData
                {
                    Id = NodeId.New(),
                    Components =
                    [
                        new MetadataData { Name = "InvisibleSun" },
                        new TransformData { Name = "Transform" },
                        new NodeFlagsData { Name = NodeFlagsComponent.DefaultName, IsVisible = false },
                        new DirectionalLightData { Name = "Sun1", AtmosphereSlot = AtmosphereLightSlot.Primary },
                        new HierarchyData { Name = HierarchyComponent.DefaultName },
                    ],
                },
                SceneTestSupport.NodeData("VisibleSun", new DirectionalLightData { Name = "Sun2", AtmosphereSlot = AtmosphereLightSlot.Primary }),
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
        var node1 = new SceneNode(scene, "Sun");
        _ = node1.AddComponent(new DirectionalLightComponent { Name = "Sun", AtmosphereSlot = AtmosphereLightSlot.Primary });
        var node2 = new SceneNode(scene, "Moon");
        _ = node2.AddComponent(new DirectionalLightComponent { Name = "Moon", AtmosphereSlot = AtmosphereLightSlot.Secondary });
        var node3 = new SceneNode(scene, "Torch");
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
