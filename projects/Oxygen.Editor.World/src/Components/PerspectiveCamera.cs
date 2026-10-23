// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Perspective camera component.
/// </summary>
public partial class PerspectiveCamera : CameraComponent
{
    private float fieldOfView = SceneSchema.PerspectiveCamera.Defaults.FieldOfViewDegrees;
    private float aspectRatio = SceneSchema.PerspectiveCamera.Defaults.AspectRatio;
    private CameraAspectMode aspectMode = SceneSchema.PerspectiveCamera.Defaults.AspectMode;

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
    /// Initializes a new instance of the <see cref="PerspectiveCamera"/> class with the scene schema's creation values.
    /// </summary>
    public PerspectiveCamera()
        : base(CameraSchema.Perspective)
    {
    }

    /// <summary>
    /// Gets or sets the field of view in degrees.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float FieldOfView
    {
        get => this.fieldOfView;
        set
        {
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, value);
            _ = this.SetProperty(ref this.fieldOfView, value);
        }
    }

    /// <summary>
    /// Gets or sets the aspect ratio (width / height).
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float AspectRatio
    {
        get => this.aspectRatio;
        set
        {
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.AspectRatio, value);
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
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.AspectMode, value);
            _ = this.SetProperty(ref this.aspectMode, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is PerspectiveCameraData candidate)
        {
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, candidate.FieldOfView);
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.AspectRatio, candidate.AspectRatio);
            PropertyChecks.Require(SceneSchema.PerspectiveCamera.AspectMode, candidate.AspectMode);
        }

        base.Hydrate(data);

        if (data is not PerspectiveCameraData pd)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.FieldOfView = pd.FieldOfView;
            this.AspectRatio = pd.AspectRatio;
            this.AspectMode = pd.AspectMode;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new PerspectiveCameraData { Id = this.Id, Name = this.Name, NearPlane = this.NearPlane, FarPlane = this.FarPlane, ApertureF = this.ApertureF, ShutterRate = this.ShutterRate, Iso = this.Iso, FieldOfView = this.FieldOfView, AspectRatio = this.AspectRatio, AspectMode = this.AspectMode };
}
