// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies that a new transform component and its data take the scene schema's defaults, and that the component and
/// loading reject a value outside the schema's range: a value that is not finite, or a zero scale axis.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TransformComponent))]
public sealed class TransformComponentTests
{
    [TestMethod]
    public void NewComponentAndData_TakeTheSchemaDefaults()
    {
        var position = new Vector3(SceneSchema.Transform.Defaults.PositionX, SceneSchema.Transform.Defaults.PositionY, SceneSchema.Transform.Defaults.PositionZ);
        var rotation = TransformConverter.EulerDegreesToQuaternion(new Vector3(SceneSchema.Transform.Defaults.RotationX, SceneSchema.Transform.Defaults.RotationY, SceneSchema.Transform.Defaults.RotationZ));
        var scale = new Vector3(SceneSchema.Transform.Defaults.ScaleX, SceneSchema.Transform.Defaults.ScaleY, SceneSchema.Transform.Defaults.ScaleZ);

        var component = new TransformComponent { Name = "Transform" };
        var data = new TransformData { Name = "Transform" };

        _ = component.LocalPosition.Should().Be(position);
        _ = component.LocalRotation.Should().Be(rotation);
        _ = component.LocalScale.Should().Be(scale);
        _ = data.Position.Should().Be(position);
        _ = data.Rotation.Should().Be(rotation);
        _ = data.Scale.Should().Be(scale);
    }

    [TestMethod]
    public void SettersOutsideTheSchemaRange_ThrowWithoutMutation()
    {
        var transform = new TransformComponent { Name = "Transform" };
        var before = transform.Dehydrate();
        Action[] changes =
        [
            () => transform.LocalScale = new Vector3(1, 0, 1),
            () => transform.LocalScale = new Vector3(float.NaN, 1, 1),
            () => transform.LocalPosition = new Vector3(0, float.PositiveInfinity, 0),
            () => transform.LocalRotation = new Quaternion(float.NaN, 0, 0, 1),
        ];

        foreach (var change in changes)
        {
            _ = change.Should().Throw<ArgumentOutOfRangeException>();
        }

        _ = transform.Dehydrate().Should().BeEquivalentTo(before);
    }

    [TestMethod]
    public void NegativeScale_IsInRange()
    {
        var transform = new TransformComponent { Name = "Transform" };

        transform.LocalScale = new Vector3(-1, 2, -3);

        _ = transform.LocalScale.Should().Be(new Vector3(-1, 2, -3));
    }

    [TestMethod]
    public void LoadingAZeroScale_IsRejectedBeforeTheSceneChanges()
    {
        var scene = new Scene { Name = "Accepted" };
        var retained = new SceneNode(scene, "Retained");
        scene.RootNodes.Add(retained);
        var node = new SceneNode(new Scene { Name = "Source" }, "Flat").Dehydrate();
        var candidate = new SceneData
        {
            Id = SceneId.New(),
            Name = "Flat",
            RootNodes =
            [
                node with
                {
                    Components = [.. node.Components.Select(static component => component is TransformData transform ? transform with { Scale = new Vector3(1, 0, 1) } : component)],
                },
            ],
        };

        var load = () => scene.Hydrate(candidate);

        _ = load.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(0f);
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(retained);
    }
}
