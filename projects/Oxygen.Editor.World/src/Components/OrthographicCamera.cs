// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
/// Orthographic camera component.
/// </summary>
public partial class OrthographicCamera : CameraComponent
{
    /// <summary>Default half of the visible height, in meters.</summary>
    public const float DefaultOrthographicSize = 10f;

    /// <summary>Default retained Fixed aspect ratio (width / height).</summary>
    public const float DefaultAspectRatio = 16f / 9f;

    private float orthographicSize = DefaultOrthographicSize;
    private float aspectRatio = DefaultAspectRatio;
    private CameraAspectMode aspectMode;

    static OrthographicCamera()
    {
        Register<OrthographicCameraData>(d =>
        {
            var c = new OrthographicCamera { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>Gets or sets half of the visible height, in meters.</summary>
    public float OrthographicSize
    {
        get => this.orthographicSize;
        set => _ = this.SetProperty(ref this.orthographicSize, value);
    }

    /// <summary>Gets or sets the retained Fixed aspect ratio (width / height).</summary>
    public float AspectRatio
    {
        get => this.aspectRatio;
        set => _ = this.SetProperty(ref this.aspectRatio, value);
    }

    /// <summary>Gets or sets framing policy without changing the retained ratio.</summary>
    public CameraAspectMode AspectMode
    {
        get => this.aspectMode;
        set
        {
            if (!Enum.IsDefined(value))
            {
                throw new ArgumentOutOfRangeException(nameof(value));
            }

            _ = this.SetProperty(ref this.aspectMode, value);
        }
    }

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
            this.AspectRatio = od.AspectRatio;
            this.AspectMode = od.AspectMode;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new OrthographicCameraData { Id = this.Id, Name = this.Name, NearPlane = this.NearPlane, FarPlane = this.FarPlane, ApertureF = this.ApertureF, ShutterRate = this.ShutterRate, Iso = this.Iso, OrthographicSize = this.OrthographicSize, AspectRatio = this.AspectRatio, AspectMode = this.AspectMode };
}
