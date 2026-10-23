// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Orthographic camera component.
/// </summary>
public partial class OrthographicCamera : CameraComponent
{
    private float orthographicSize = SceneSchema.OrthographicCamera.Defaults.OrthographicSize;
    private float aspectRatio = SceneSchema.OrthographicCamera.Defaults.AspectRatio;
    private CameraAspectMode aspectMode = SceneSchema.OrthographicCamera.Defaults.AspectMode;

    static OrthographicCamera()
    {
        Register<OrthographicCameraData>(d =>
        {
            var c = new OrthographicCamera { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>Initializes a new instance of the <see cref="OrthographicCamera"/> class with the scene schema's creation values.</summary>
    public OrthographicCamera()
        : base(CameraSchema.Orthographic)
    {
    }

    /// <summary>Gets or sets half of the visible height, in meters.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float OrthographicSize
    {
        get => this.orthographicSize;
        set
        {
            PropertyChecks.Require(SceneSchema.OrthographicCamera.OrthographicSize, value);
            _ = this.SetProperty(ref this.orthographicSize, value);
        }
    }

    /// <summary>Gets or sets the retained Fixed aspect ratio (width / height).</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float AspectRatio
    {
        get => this.aspectRatio;
        set
        {
            PropertyChecks.Require(SceneSchema.OrthographicCamera.AspectRatio, value);
            _ = this.SetProperty(ref this.aspectRatio, value);
        }
    }

    /// <summary>Gets or sets framing policy without changing the retained ratio.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a mode the scene schema declares.</exception>
    public CameraAspectMode AspectMode
    {
        get => this.aspectMode;
        set
        {
            PropertyChecks.Require(SceneSchema.OrthographicCamera.AspectMode, value);
            _ = this.SetProperty(ref this.aspectMode, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is OrthographicCameraData candidate)
        {
            PropertyChecks.Require(SceneSchema.OrthographicCamera.OrthographicSize, candidate.OrthographicSize);
            PropertyChecks.Require(SceneSchema.OrthographicCamera.AspectRatio, candidate.AspectRatio);
            PropertyChecks.Require(SceneSchema.OrthographicCamera.AspectMode, candidate.AspectMode);
        }

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
