// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Perspective camera component.
/// </summary>
public partial class PerspectiveCamera : CameraComponent
{
    private float fieldOfView = DefaultFieldOfViewDegrees;
    private float aspectRatio = DefaultAspectRatio;

    /// <summary>
    /// Default vertical field of view stored by the editor, in degrees.
    /// </summary>
    public const float DefaultFieldOfViewDegrees = 60f;

    /// <summary>
    /// Default camera aspect ratio (width / height).
    /// </summary>
    public const float DefaultAspectRatio = 16f / 9f;

    static PerspectiveCamera()
    {
        Register<PerspectiveCameraData>(d =>
        {
            var c = new PerspectiveCamera { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>
    /// Gets or sets the field of view in degrees.
    /// </summary>
    public float FieldOfView
    {
        get => this.fieldOfView;
        set => _ = this.SetProperty(ref this.fieldOfView, value);
    }

    /// <summary>
    /// Gets or sets the aspect ratio (width / height).
    /// </summary>
    public float AspectRatio
    {
        get => this.aspectRatio;
        set => _ = this.SetProperty(ref this.aspectRatio, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not PerspectiveCameraData pd)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.FieldOfView = pd.FieldOfView;
            this.AspectRatio = pd.AspectRatio;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new PerspectiveCameraData { Id = this.Id, Name = this.Name, NearPlane = this.NearPlane, FarPlane = this.FarPlane, FieldOfView = this.FieldOfView, AspectRatio = this.AspectRatio };
}
