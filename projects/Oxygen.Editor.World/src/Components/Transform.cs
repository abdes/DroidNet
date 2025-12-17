// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

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
    private Vector3 localPosition;
    private Quaternion localRotation = Quaternion.Identity;
    private Vector3 localScale = Vector3.One;

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
    public Vector3 LocalPosition
    {
        get => this.localPosition;
        set => _ = this.SetProperty(ref this.localPosition, value);
    }

    /// <summary>
    ///     Gets or sets the local rotation of the scene node (as a quaternion).
    /// </summary>
    /// <value>
    ///     A <see cref="Quaternion" /> representing the local rotation of the scene node.
    /// </value>
    public Quaternion LocalRotation
    {
        get => this.localRotation;
        set => _ = this.SetProperty(ref this.localRotation, value);
    }

    /// <summary>
    ///     Gets or sets the local scale of the scene node.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the local scale of the scene node.
    /// </value>
    public Vector3 LocalScale
    {
        get => this.localScale;
        set => _ = this.SetProperty(ref this.localScale, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not TransformData d)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.LocalPosition = d.Position;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new TransformData
        {
            Name = this.Name,
            Position = this.LocalPosition,
            Rotation = this.LocalRotation,
            Scale = this.LocalScale,
        };
}
