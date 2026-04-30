// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Authored directional light component.
/// </summary>
public sealed partial class DirectionalLightComponent : LightComponent
{
    /// <summary>
    /// Default illuminance for a sun-like directional light, in lux.
    /// </summary>
    public const float DefaultIntensityLux = 100_000f;

    /// <summary>
    /// Default sun angular diameter, in radians.
    /// </summary>
    public const float DefaultAngularSizeRadians = 0.00935f;

    /// <summary>
    /// Default number of directional-light shadow cascades.
    /// </summary>
    public const int DefaultCascadeCount = 4;

    /// <summary>
    /// Default maximum directional-light shadow distance, in meters.
    /// </summary>
    public const float DefaultMaxShadowDistance = 160f;

    /// <summary>
    /// Default generated CSM distribution exponent.
    /// </summary>
    public const float DefaultDistributionExponent = 3f;

    /// <summary>
    /// Default blend fraction between cascades.
    /// </summary>
    public const float DefaultTransitionFraction = 0.1f;

    /// <summary>
    /// Default fadeout fraction near the end of the shadow range.
    /// </summary>
    public const float DefaultDistanceFadeoutFraction = 0.1f;

    /// <summary>
    /// Default manual cascade distances, in meters.
    /// </summary>
    public static readonly Vector4 DefaultCascadeDistances = new(8f, 24f, 64f, 160f);

    /// <summary>
    /// Default local rotation for new directional lights.
    /// </summary>
    public static readonly Quaternion DefaultLocalRotation = Quaternion.CreateFromYawPitchRoll(0f, 0.7853982f, 0f);

    private float intensityLux = DefaultIntensityLux;
    private float angularSizeRadians = DefaultAngularSizeRadians;
    private bool environmentContribution = true;
    private bool isSunLight = true;
    private int cascadeCount = DefaultCascadeCount;
    private DirectionalCsmSplitMode splitMode = DirectionalCsmSplitMode.Generated;
    private float maxShadowDistance = DefaultMaxShadowDistance;
    private Vector4 cascadeDistances = DefaultCascadeDistances;
    private float distributionExponent = DefaultDistributionExponent;
    private float transitionFraction = DefaultTransitionFraction;
    private float distanceFadeoutFraction = DefaultDistanceFadeoutFraction;

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
    /// Gets or sets directional-light illuminance, in lux.
    /// </summary>
    public float IntensityLux
    {
        get => this.intensityLux;
        set => _ = this.SetProperty(ref this.intensityLux, value);
    }

    /// <summary>
    /// Gets or sets the apparent angular size, in radians.
    /// </summary>
    public float AngularSizeRadians
    {
        get => this.angularSizeRadians;
        set => _ = this.SetProperty(ref this.angularSizeRadians, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether this light contributes to the scene environment.
    /// </summary>
    public bool EnvironmentContribution
    {
        get => this.environmentContribution;
        set => _ = this.SetProperty(ref this.environmentContribution, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether this directional light is the active sun.
    /// </summary>
    public bool IsSunLight
    {
        get => this.isSunLight;
        set => _ = this.SetProperty(ref this.isSunLight, value);
    }

    /// <summary>
    /// Gets or sets the number of directional shadow cascades.
    /// </summary>
    public int CascadeCount
    {
        get => this.cascadeCount;
        set => _ = this.SetProperty(ref this.cascadeCount, value);
    }

    /// <summary>
    /// Gets or sets how CSM split distances are produced.
    /// </summary>
    public DirectionalCsmSplitMode SplitMode
    {
        get => this.splitMode;
        set => _ = this.SetProperty(ref this.splitMode, value);
    }

    /// <summary>
    /// Gets or sets the maximum directional shadow distance, in meters.
    /// </summary>
    public float MaxShadowDistance
    {
        get => this.maxShadowDistance;
        set => _ = this.SetProperty(ref this.maxShadowDistance, value);
    }

    /// <summary>
    /// Gets or sets the manual cascade distances, in meters.
    /// </summary>
    public Vector4 CascadeDistances
    {
        get => this.cascadeDistances;
        set => _ = this.SetProperty(ref this.cascadeDistances, value);
    }

    /// <summary>
    /// Gets or sets the generated CSM distribution exponent.
    /// </summary>
    public float DistributionExponent
    {
        get => this.distributionExponent;
        set => _ = this.SetProperty(ref this.distributionExponent, value);
    }

    /// <summary>
    /// Gets or sets the cascade transition fraction.
    /// </summary>
    public float TransitionFraction
    {
        get => this.transitionFraction;
        set => _ = this.SetProperty(ref this.transitionFraction, value);
    }

    /// <summary>
    /// Gets or sets the shadow distance fadeout fraction.
    /// </summary>
    public float DistanceFadeoutFraction
    {
        get => this.distanceFadeoutFraction;
        set => _ = this.SetProperty(ref this.distanceFadeoutFraction, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);
        if (data is not DirectionalLightData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.IntensityLux = light.IntensityLux;
            this.AngularSizeRadians = light.AngularSizeRadians;
            this.EnvironmentContribution = light.EnvironmentContribution;
            this.IsSunLight = light.IsSunLight;
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
            Mobility = this.Mobility,
            CastsShadows = this.CastsShadows,
            Shadow = this.DehydrateShadow(),
            ExposureCompensation = this.ExposureCompensation,
            IntensityLux = this.IntensityLux,
            AngularSizeRadians = this.AngularSizeRadians,
            EnvironmentContribution = this.EnvironmentContribution,
            IsSunLight = this.IsSunLight,
            CascadeCount = this.CascadeCount,
            SplitMode = this.SplitMode,
            MaxShadowDistance = this.MaxShadowDistance,
            CascadeDistances = this.CascadeDistances,
            DistributionExponent = this.DistributionExponent,
            TransitionFraction = this.TransitionFraction,
            DistanceFadeoutFraction = this.DistanceFadeoutFraction,
        };
}
