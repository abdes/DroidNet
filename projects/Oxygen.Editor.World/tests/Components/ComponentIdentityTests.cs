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
/// Verifies component identity persistence and identity repair on duplicate or empty GUIDs.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ComponentId))]
public sealed class ComponentIdentityTests
{
    [TestMethod]
    public void ComponentIdentity_ShouldRoundTripThroughDto()
    {
        var componentId = ComponentId.New();
        var camera = new PerspectiveCamera
        {
            Id = componentId,
            Name = "Main Camera",
        };

        var dto = (PerspectiveCameraData)camera.Dehydrate();
        var recreated = (PerspectiveCamera)GameComponent.CreateAndHydrate(dto);

        _ = dto.Id.Should().Be(componentId);
        _ = recreated.Id.Should().Be(componentId);
    }

    [TestMethod]
    public void SceneNodeHydrate_ShouldRepairEmptyOrDuplicateComponentIdentities()
    {
        var scene = new Scene { Name = "Scene" };
        var duplicateId = ComponentId.FromValue(Guid.Parse("11111111-1111-1111-1111-111111111111"));
        var data = new SceneNodeData
        {
            Id = NodeId.New(),
            Name = "Cube",
            Components =
            [
                new TransformData { Id = ComponentId.Empty, Name = "Transform" },
                new GeometryComponentData
                {
                    Id = ComponentId.Empty,
                    Name = "Geometry",
                    GeometryUri = "asset:///Engine/Generated/BasicShapes/Cube",
                },
                new PerspectiveCameraData
                {
                    AspectMode = Oxygen.Managed.Core.CameraAspectMode.Auto,
                    Id = duplicateId,
                    Name = "Camera",
                },
                new OrthographicCameraData
                {
                    AspectMode = Oxygen.Managed.Core.CameraAspectMode.Auto,
                    Id = duplicateId,
                    Name = "Camera 2",
                },
            ],
        };

        var node = SceneNode.CreateAndHydrate(scene, data);

        _ = node.Components.Should().OnlyHaveUniqueItems(static component => component.Id);
        _ = node.Components.Should().NotContain(component => component.Id.IsEmpty);
    }
}
