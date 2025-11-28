// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Perspective camera component.
/// </summary>
public partial class PerspectiveCamera : CameraComponent
{
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
    public float FieldOfView { get; set; } = 60f;

    /// <summary>
    /// Gets or sets the aspect ratio (width / height).
    /// </summary>
    public float AspectRatio { get; set; } = 16f / 9f;

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
        => new PerspectiveCameraData { Name = this.Name, NearPlane = this.NearPlane, FarPlane = this.FarPlane, FieldOfView = this.FieldOfView, AspectRatio = this.AspectRatio };
}
