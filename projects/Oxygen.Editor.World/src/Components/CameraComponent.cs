// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Base class for camera components.
/// Camera domain objects implement Hydrate/Dehydrate via <see cref="ComponentData"/> and are created
/// by factories registered on concrete DTO types (see <c>PerspectiveCamera</c> and <c>OrthographicCamera</c>).
/// </summary>
public abstract class CameraComponent : GameComponent
{
    private readonly CameraSchema schema;
    private float apertureF;
    private float shutterRate;
    private float iso;
    private float nearPlane;
    private float farPlane;

    /// <summary>Initializes a new instance of the <see cref="CameraComponent"/> class with the creation values of its kind.</summary>
    /// <param name="schema">The scene schema of the camera kind.</param>
    private protected CameraComponent(CameraSchema schema)
    {
        this.schema = schema;
        this.nearPlane = schema.NearPlane;
        this.farPlane = schema.FarPlane;
        this.apertureF = schema.ApertureF;
        this.shutterRate = schema.ShutterRate;
        this.iso = schema.Iso;
    }

    /// <summary>
    /// Gets or sets the near clipping plane distance.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float NearPlane
    {
        get => this.nearPlane;
        set
        {
            PropertyChecks.Require(this.schema.NearPlaneId, value);
            _ = this.SetProperty(ref this.nearPlane, value);
        }
    }

    /// <summary>
    /// Gets or sets the far clipping plane distance.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float FarPlane
    {
        get => this.farPlane;
        set
        {
            PropertyChecks.Require(this.schema.FarPlaneId, value);
            _ = this.SetProperty(ref this.farPlane, value);
        }
    }

    /// <summary>Gets or sets the aperture as an f-number.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float ApertureF
    {
        get => this.apertureF;
        set
        {
            PropertyChecks.Require(this.schema.ApertureFId, value);
            _ = this.SetProperty(ref this.apertureF, value);
        }
    }

    /// <summary>Gets or sets the shutter rate in reciprocal seconds.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float ShutterRate
    {
        get => this.shutterRate;
        set
        {
            PropertyChecks.Require(this.schema.ShutterRateId, value);
            _ = this.SetProperty(ref this.shutterRate, value);
        }
    }

    /// <summary>Gets or sets the ISO sensitivity.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float Iso
    {
        get => this.iso;
        set
        {
            PropertyChecks.Require(this.schema.IsoId, value);
            _ = this.SetProperty(ref this.iso, value);
        }
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        if (data is CameraComponentData cameraData)
        {
            PropertyChecks.Require(this.schema.NearPlaneId, cameraData.NearPlane);
            PropertyChecks.Require(this.schema.FarPlaneId, cameraData.FarPlane);
            PropertyChecks.Require(this.schema.ApertureFId, cameraData.ApertureF);
            PropertyChecks.Require(this.schema.ShutterRateId, cameraData.ShutterRate);
            PropertyChecks.Require(this.schema.IsoId, cameraData.Iso);
        }

        base.Hydrate(data);

        if (data is not CameraComponentData cd)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.NearPlane = cd.NearPlane;
            this.FarPlane = cd.FarPlane;
            this.apertureF = cd.ApertureF;
            this.shutterRate = cd.ShutterRate;
            this.iso = cd.Iso;
        }
    }

    /// <summary>
    /// Dehydrate is implemented by concrete camera components which return the
    /// correct concrete DTO type (e.g. <see cref="PerspectiveCameraData"/>).
    /// </summary>
    /// <returns>
    /// Throws <see cref="System.NotSupportedException"/> in the base class. Concrete camera components return the appropriate <see cref="ComponentData"/> DTO.
    /// </returns>
    public override ComponentData Dehydrate() => throw new System.NotSupportedException();
}
