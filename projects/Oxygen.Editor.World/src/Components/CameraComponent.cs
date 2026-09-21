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
public abstract partial class CameraComponent : GameComponent
{
    /// <summary>Default authored aperture, matching the native scene contract.</summary>
    public const float DefaultApertureF = 11f;

    /// <summary>Default shutter rate in reciprocal seconds.</summary>
    public const float DefaultShutterRate = 125f;

    /// <summary>Default ISO sensitivity.</summary>
    public const float DefaultIso = 100f;

    private float apertureF = DefaultApertureF;
    private float shutterRate = DefaultShutterRate;
    private float iso = DefaultIso;

    private float nearPlane = 0.1f;
    private float farPlane = 1000f;

    /// <summary>
    /// Gets or sets the near clipping plane distance.
    /// </summary>
    public float NearPlane
    {
        get => this.nearPlane;
        set => _ = this.SetProperty(ref this.nearPlane, value);
    }

    /// <summary>
    /// Gets or sets the far clipping plane distance.
    /// </summary>
    public float FarPlane
    {
        get => this.farPlane;
        set => _ = this.SetProperty(ref this.farPlane, value);
    }

    /// <summary>Gets or sets the aperture as an f-number.</summary>
    public float ApertureF
    {
        get => this.apertureF;
        set
        {
            ValidatePhysicalValue(value, nameof(ApertureF));
            _ = this.SetProperty(ref this.apertureF, value);
        }
    }

    /// <summary>Gets or sets the shutter rate in reciprocal seconds.</summary>
    public float ShutterRate
    {
        get => this.shutterRate;
        set
        {
            ValidatePhysicalValue(value, nameof(ShutterRate));
            _ = this.SetProperty(ref this.shutterRate, value);
        }
    }

    /// <summary>Gets or sets the ISO sensitivity.</summary>
    public float Iso
    {
        get => this.iso;
        set
        {
            ValidatePhysicalValue(value, nameof(Iso));
            _ = this.SetProperty(ref this.iso, value);
        }
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        if (data is CameraComponentData cameraData)
        {
            ValidatePhysicalValue(cameraData.ApertureF, nameof(ApertureF));
            ValidatePhysicalValue(cameraData.ShutterRate, nameof(ShutterRate));
            ValidatePhysicalValue(cameraData.Iso, nameof(Iso));
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

    // Intentionally no IPersistent<TData> on the domain types; concrete components register
    // factories for their DTO types and Hydrate/Dehydrate using ComponentData-based methods.
    private static void ValidatePhysicalValue(float value, string field)
    {
        if (!float.IsFinite(value) || value <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(value), value, $"{field} must be finite and positive.");
        }
    }

}
