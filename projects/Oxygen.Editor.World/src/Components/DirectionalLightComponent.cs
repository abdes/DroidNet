// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Authored directional light component.
/// </summary>
public sealed partial class DirectionalLightComponent : LightComponent
{
    /// <summary>
    /// Default local rotation for new directional lights.
    /// </summary>
    public static readonly Quaternion DefaultLocalRotation = Quaternion.CreateFromYawPitchRoll(0f, 0.7853982f, 0f);

    private float intensityLux = SceneSchema.DirectionalLight.Defaults.IntensityLux;
    private float angularSizeRadians = SceneSchema.DirectionalLight.Defaults.AngularSizeRadians;
    private AtmosphereLightSlot atmosphereSlot = SceneSchema.DirectionalLight.Defaults.AtmosphereSlot;
    private bool usePerPixelAtmosphereTransmittance = SceneSchema.DirectionalLight.Defaults.UsePerPixelAtmosphereTransmittance;
    private Vector3 atmosphereDiskLuminanceScaleRgb = SceneSchema.DirectionalLight.Defaults.AtmosphereDiskLuminanceScaleRgb;
    private int cascadeCount = SceneSchema.DirectionalLight.Defaults.CascadeCount;
    private DirectionalCsmSplitMode splitMode = SceneSchema.DirectionalLight.Defaults.SplitMode;
    private float maxShadowDistance = SceneSchema.DirectionalLight.Defaults.MaxShadowDistance;
    private Vector4 cascadeDistances = new(
        SceneSchema.DirectionalLight.Defaults.CascadeDistance0,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance1,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance2,
        SceneSchema.DirectionalLight.Defaults.CascadeDistance3);

    private float distributionExponent = SceneSchema.DirectionalLight.Defaults.DistributionExponent;
    private float transitionFraction = SceneSchema.DirectionalLight.Defaults.TransitionFraction;
    private float distanceFadeoutFraction = SceneSchema.DirectionalLight.Defaults.DistanceFadeoutFraction;

    static DirectionalLightComponent()
    {
        Register<DirectionalLightData>(d =>
        {
            var c = new DirectionalLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightComponent"/> class with the scene schema's creation values.
    /// </summary>
    public DirectionalLightComponent()
        : base(LightSchema.Directional)
    {
    }

    /// <summary>
    /// Gets or sets directional-light illuminance, in lux.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float IntensityLux
    {
        get => this.intensityLux;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.IntensityLux, value);
            _ = this.SetProperty(ref this.intensityLux, value);
        }
    }

    /// <summary>
    /// Gets or sets the apparent angular size, in radians.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float AngularSizeRadians
    {
        get => this.angularSizeRadians;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.AngularSizeRadians, value);
            _ = this.SetProperty(ref this.angularSizeRadians, value);
        }
    }

    /// <summary>Gets or sets the explicit atmosphere source slot.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a member the scene schema declares.</exception>
    public AtmosphereLightSlot AtmosphereSlot
    {
        get => this.atmosphereSlot;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.AtmosphereSlot, value);
            _ = this.SetProperty(ref this.atmosphereSlot, value);
        }
    }

    /// <summary>Gets or sets a value indicating whether surface atmosphere transmittance is evaluated per pixel.</summary>
    public bool UsePerPixelAtmosphereTransmittance
    {
        get => this.usePerPixelAtmosphereTransmittance;
        set => _ = this.SetProperty(ref this.usePerPixelAtmosphereTransmittance, value);
    }

    /// <summary>Gets or sets the analytic disk's linear RGB luminance multiplier.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public Vector3 AtmosphereDiskLuminanceScaleRgb
    {
        get => this.atmosphereDiskLuminanceScaleRgb;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, value);
            _ = this.SetProperty(ref this.atmosphereDiskLuminanceScaleRgb, value);
        }
    }

    /// <summary>
    /// Gets or sets the number of directional shadow cascades.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public int CascadeCount
    {
        get => this.cascadeCount;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeCount, value);
            _ = this.SetProperty(ref this.cascadeCount, value);
        }
    }

    /// <summary>
    /// Gets or sets how CSM split distances are produced.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a member the scene schema declares.</exception>
    public DirectionalCsmSplitMode SplitMode
    {
        get => this.splitMode;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.SplitMode, value);
            _ = this.SetProperty(ref this.splitMode, value);
        }
    }

    /// <summary>
    /// Gets or sets the maximum directional shadow distance, in meters.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float MaxShadowDistance
    {
        get => this.maxShadowDistance;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.MaxShadowDistance, value);
            _ = this.SetProperty(ref this.maxShadowDistance, value);
        }
    }

    /// <summary>
    /// Gets or sets the manual cascade distances, in meters.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">A distance is outside the range the scene schema declares.</exception>
    public Vector4 CascadeDistances
    {
        get => this.cascadeDistances;
        set
        {
            RequireCascadeDistances(value);
            _ = this.SetProperty(ref this.cascadeDistances, value);
        }
    }

    /// <summary>
    /// Gets or sets the generated CSM distribution exponent.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float DistributionExponent
    {
        get => this.distributionExponent;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.DistributionExponent, value);
            _ = this.SetProperty(ref this.distributionExponent, value);
        }
    }

    /// <summary>
    /// Gets or sets the cascade transition fraction.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float TransitionFraction
    {
        get => this.transitionFraction;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.TransitionFraction, value);
            _ = this.SetProperty(ref this.transitionFraction, value);
        }
    }

    /// <summary>
    /// Gets or sets the shadow distance fadeout fraction.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float DistanceFadeoutFraction
    {
        get => this.distanceFadeoutFraction;
        set
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.DistanceFadeoutFraction, value);
            _ = this.SetProperty(ref this.distanceFadeoutFraction, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    /// <exception cref="ArgumentException">The values break a light rule; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is DirectionalLightData candidate)
        {
            PropertyChecks.Require(SceneSchema.DirectionalLight.IntensityLux, candidate.IntensityLux);
            PropertyChecks.Require(SceneSchema.DirectionalLight.AngularSizeRadians, candidate.AngularSizeRadians);
            PropertyChecks.Require(SceneSchema.DirectionalLight.AtmosphereSlot, candidate.AtmosphereSlot);
            PropertyChecks.Require(SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, candidate.AtmosphereDiskLuminanceScaleRgb);
            PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeCount, candidate.CascadeCount);
            PropertyChecks.Require(SceneSchema.DirectionalLight.SplitMode, candidate.SplitMode);
            PropertyChecks.Require(SceneSchema.DirectionalLight.MaxShadowDistance, candidate.MaxShadowDistance);
            RequireCascadeDistances(candidate.CascadeDistances);
            PropertyChecks.Require(SceneSchema.DirectionalLight.DistributionExponent, candidate.DistributionExponent);
            PropertyChecks.Require(SceneSchema.DirectionalLight.TransitionFraction, candidate.TransitionFraction);
            PropertyChecks.Require(SceneSchema.DirectionalLight.DistanceFadeoutFraction, candidate.DistanceFadeoutFraction);
        }

        base.Hydrate(data);
        if (data is not DirectionalLightData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.IntensityLux = light.IntensityLux;
            this.AngularSizeRadians = light.AngularSizeRadians;
            this.AtmosphereSlot = light.AtmosphereSlot;
            this.UsePerPixelAtmosphereTransmittance = light.UsePerPixelAtmosphereTransmittance;
            this.AtmosphereDiskLuminanceScaleRgb = light.AtmosphereDiskLuminanceScaleRgb;
            this.CascadeCount = light.CascadeCount;
            this.SplitMode = light.SplitMode;
            this.MaxShadowDistance = light.MaxShadowDistance;
            this.CascadeDistances = light.CascadeDistances;
            this.DistributionExponent = light.DistributionExponent;
            this.TransitionFraction = light.TransitionFraction;
            this.DistanceFadeoutFraction = light.DistanceFadeoutFraction;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new DirectionalLightData
        {
            Id = this.Id,
            Name = this.Name,
            AffectsWorld = this.AffectsWorld,
            Color = this.Color,

            CastsShadows = this.CastsShadows,
            Shadow = this.DehydrateShadow(),
            ExposureCompensation = this.ExposureCompensation,
            IntensityLux = this.IntensityLux,
            AngularSizeRadians = this.AngularSizeRadians,
            AtmosphereSlot = this.AtmosphereSlot,
            UsePerPixelAtmosphereTransmittance = this.UsePerPixelAtmosphereTransmittance,
            AtmosphereDiskLuminanceScaleRgb = this.AtmosphereDiskLuminanceScaleRgb,
            CascadeCount = this.CascadeCount,
            SplitMode = this.SplitMode,
            MaxShadowDistance = this.MaxShadowDistance,
            CascadeDistances = this.CascadeDistances,
            DistributionExponent = this.DistributionExponent,
            TransitionFraction = this.TransitionFraction,
            DistanceFadeoutFraction = this.DistanceFadeoutFraction,
        };

    // Each manual distance has its own schema property.
    private static void RequireCascadeDistances(Vector4 distances)
    {
        PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeDistance0, distances.X);
        PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeDistance1, distances.Y);
        PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeDistance2, distances.Z);
        PropertyChecks.Require(SceneSchema.DirectionalLight.CascadeDistance3, distances.W);
    }
}
