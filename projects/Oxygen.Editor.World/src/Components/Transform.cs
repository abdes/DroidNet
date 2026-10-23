// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Components;

/// <summary>
///     Represents a transform component of a scene node, specifying its position, rotation, and scale in the scene.
/// </summary>
/// <remarks>
///     The <see cref="TransformComponent" /> class represents a transform component of a scene node. It includes properties for
///     the position, rotation, and scale of the scene node within the scene. The class provides methods for getting and
///     setting these properties.
/// </remarks>
public partial class TransformComponent : GameComponent
{
    private Vector3 localPosition = new(
        SceneSchema.Transform.Defaults.PositionX,
        SceneSchema.Transform.Defaults.PositionY,
        SceneSchema.Transform.Defaults.PositionZ);

    private Quaternion localRotation = TransformConverter.EulerDegreesToQuaternion(new(
        SceneSchema.Transform.Defaults.RotationX,
        SceneSchema.Transform.Defaults.RotationY,
        SceneSchema.Transform.Defaults.RotationZ));

    private Vector3 localScale = new(
        SceneSchema.Transform.Defaults.ScaleX,
        SceneSchema.Transform.Defaults.ScaleY,
        SceneSchema.Transform.Defaults.ScaleZ);

    static TransformComponent()
    {
        Register<TransformData>(d =>
        {
            var t = new TransformComponent { Name = "Transform" };
            t.Hydrate(d);
            return t;
        });
    }

    /// <summary>
    ///     Gets or sets the local position of the scene node.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the local position of the scene node.
    /// </value>
    /// <exception cref="ArgumentOutOfRangeException">An axis is outside the range the scene schema declares.</exception>
    public Vector3 LocalPosition
    {
        get => this.localPosition;
        set
        {
            RequirePosition(value);
            _ = this.SetProperty(ref this.localPosition, value);
        }
    }

    /// <summary>
    /// Gets a value indicating whether transform component is fundamental to a node and must not be deleted; report locked.
    /// </summary>
    public override bool IsLocked => true;

    /// <summary>
    ///     Gets or sets the local rotation of the scene node (as a quaternion).
    /// </summary>
    /// <value>
    ///     A <see cref="Quaternion" /> representing the local rotation of the scene node.
    /// </value>
    /// <exception cref="ArgumentOutOfRangeException">The rotation is not finite.</exception>
    public Quaternion LocalRotation
    {
        get => this.localRotation;
        set
        {
            RequireRotation(value);
            _ = this.SetProperty(ref this.localRotation, value);
        }
    }

    /// <summary>
    ///     Gets or sets the local scale of the scene node.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the local scale of the scene node.
    /// </value>
    /// <exception cref="ArgumentOutOfRangeException">An axis is outside the range the scene schema declares, such as zero.</exception>
    public Vector3 LocalScale
    {
        get => this.localScale;
        set
        {
            RequireScale(value);
            _ = this.SetProperty(ref this.localScale, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is TransformData candidate)
        {
            RequirePosition(candidate.Position);
            RequireRotation(candidate.Rotation);
            RequireScale(candidate.Scale);
        }

        base.Hydrate(data);

        if (data is not TransformData d)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.LocalPosition = d.Position;
            this.LocalRotation = d.Rotation;
            this.LocalScale = d.Scale;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new TransformData
        {
            Id = this.Id,
            Name = this.Name,
            Position = this.LocalPosition,
            Rotation = this.LocalRotation,
            Scale = this.LocalScale,
        };

    private static void RequirePosition(Vector3 position)
    {
        PropertyChecks.Require(SceneSchema.Transform.PositionX, position.X);
        PropertyChecks.Require(SceneSchema.Transform.PositionY, position.Y);
        PropertyChecks.Require(SceneSchema.Transform.PositionZ, position.Z);
    }

    // The schema ranges the Euler angles a rotation reads as; a rotation that is not finite reads as angles that are not.
    private static void RequireRotation(Quaternion rotation)
    {
        var euler = TransformConverter.QuaternionToEulerDegrees(rotation);
        PropertyChecks.Require(SceneSchema.Transform.RotationX, euler.X, nameof(rotation));
        PropertyChecks.Require(SceneSchema.Transform.RotationY, euler.Y, nameof(rotation));
        PropertyChecks.Require(SceneSchema.Transform.RotationZ, euler.Z, nameof(rotation));
    }

    private static void RequireScale(Vector3 scale)
    {
        PropertyChecks.Require(SceneSchema.Transform.ScaleX, scale.X);
        PropertyChecks.Require(SceneSchema.Transform.ScaleY, scale.Y);
        PropertyChecks.Require(SceneSchema.Transform.ScaleZ, scale.Z);
    }
}
