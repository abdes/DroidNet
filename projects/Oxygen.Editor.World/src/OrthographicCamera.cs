// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Orthographic camera component.
/// </summary>
public partial class OrthographicCamera : CameraComponent
{
    static OrthographicCamera()
    {
        Register<OrthographicCameraData>(d =>
        {
            var c = new OrthographicCamera { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>
    /// Gets or sets the size of the orthographic projection.
    /// </summary>
    public float OrthographicSize { get; set; } = 10f;

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not OrthographicCameraData od)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.OrthographicSize = od.OrthographicSize;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new OrthographicCameraData { Name = this.Name, NearPlane = this.NearPlane, FarPlane = this.FarPlane, OrthographicSize = this.OrthographicSize };
}
