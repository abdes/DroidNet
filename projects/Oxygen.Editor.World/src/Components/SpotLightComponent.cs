// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Authored spot light component.
/// </summary>
public sealed partial class SpotLightComponent : LightComponent
{
    private float luminousFluxLumens = SceneSchema.SpotLight.Defaults.LuminousFluxLumens;
    private float range = SceneSchema.SpotLight.Defaults.Range;
    private float sourceRadius = SceneSchema.SpotLight.Defaults.SourceRadius;
    private float innerConeAngleRadians = SceneSchema.SpotLight.Defaults.InnerConeAngleRadians;
    private float outerConeAngleRadians = SceneSchema.SpotLight.Defaults.OuterConeAngleRadians;

    static SpotLightComponent()
    {
        Register<SpotLightData>(d =>
        {
            var c = new SpotLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SpotLightComponent"/> class with the scene schema's creation values.
    /// </summary>
    public SpotLightComponent()
        : base(LightSchema.Spot)
    {
    }

    /// <summary>
    /// Gets or sets luminous flux, in lumens.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float LuminousFluxLumens
    {
        get => this.luminousFluxLumens;
        set
        {
            PropertyChecks.Require(SceneSchema.SpotLight.LuminousFluxLumens, value);
            _ = this.SetProperty(ref this.luminousFluxLumens, value);
        }
    }

    /// <summary>
    /// Gets or sets the influence range, in meters.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float Range
    {
        get => this.range;
        set
        {
            PropertyChecks.Require(SceneSchema.SpotLight.Range, value);
            _ = this.SetProperty(ref this.range, value);
        }
    }

    /// <summary>
    /// Gets or sets the source radius, in meters.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float SourceRadius
    {
        get => this.sourceRadius;
        set
        {
            PropertyChecks.Require(SceneSchema.SpotLight.SourceRadius, value);
            _ = this.SetProperty(ref this.sourceRadius, value);
        }
    }

    /// <summary>
    /// Gets or sets the inner cone angle, in radians.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float InnerConeAngleRadians
    {
        get => this.innerConeAngleRadians;
        set
        {
            PropertyChecks.Require(SceneSchema.SpotLight.InnerConeAngleRadians, value);
            _ = this.SetProperty(ref this.innerConeAngleRadians, value);
        }
    }

    /// <summary>
    /// Gets or sets the outer cone angle, in radians.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is outside the range the scene schema declares.</exception>
    public float OuterConeAngleRadians
    {
        get => this.outerConeAngleRadians;
        set
        {
            PropertyChecks.Require(SceneSchema.SpotLight.OuterConeAngleRadians, value);
            _ = this.SetProperty(ref this.outerConeAngleRadians, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    /// <exception cref="ArgumentException">The values break a light rule; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is SpotLightData candidate)
        {
            PropertyChecks.Require(SceneSchema.SpotLight.LuminousFluxLumens, candidate.LuminousFluxLumens);
            PropertyChecks.Require(SceneSchema.SpotLight.Range, candidate.Range);
            PropertyChecks.Require(SceneSchema.SpotLight.SourceRadius, candidate.SourceRadius);
            PropertyChecks.Require(SceneSchema.SpotLight.InnerConeAngleRadians, candidate.InnerConeAngleRadians);
            PropertyChecks.Require(SceneSchema.SpotLight.OuterConeAngleRadians, candidate.OuterConeAngleRadians);
        }

        base.Hydrate(data);
        if (data is not SpotLightData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.LuminousFluxLumens = light.LuminousFluxLumens;
            this.Range = light.Range;
            this.SourceRadius = light.SourceRadius;

            this.InnerConeAngleRadians = light.InnerConeAngleRadians;
            this.OuterConeAngleRadians = light.OuterConeAngleRadians;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new SpotLightData
        {
            Id = this.Id,
            Name = this.Name,
            AffectsWorld = this.AffectsWorld,
            Color = this.Color,

            CastsShadows = this.CastsShadows,
            Shadow = this.DehydrateShadow(),
            ExposureCompensation = this.ExposureCompensation,
            LuminousFluxLumens = this.LuminousFluxLumens,
            Range = this.Range,
            SourceRadius = this.SourceRadius,

            InnerConeAngleRadians = this.InnerConeAngleRadians,
            OuterConeAngleRadians = this.OuterConeAngleRadians,
        };
}
