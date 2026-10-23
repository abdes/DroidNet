// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Authored point light component.
/// </summary>
public sealed partial class PointLightComponent : LightComponent
{
    private float luminousFluxLumens = SceneSchema.PointLight.Defaults.LuminousFluxLumens;
    private float range = SceneSchema.PointLight.Defaults.Range;
    private float sourceRadius = SceneSchema.PointLight.Defaults.SourceRadius;

    static PointLightComponent()
    {
        Register<PointLightData>(d =>
        {
            var c = new PointLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="PointLightComponent"/> class with the scene schema's creation values.
    /// </summary>
    public PointLightComponent()
        : base(LightSchema.Point)
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
            PropertyChecks.Require(SceneSchema.PointLight.LuminousFluxLumens, value);
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
            PropertyChecks.Require(SceneSchema.PointLight.Range, value);
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
            PropertyChecks.Require(SceneSchema.PointLight.SourceRadius, value);
            _ = this.SetProperty(ref this.sourceRadius, value);
        }
    }

    /// <inheritdoc/>
    /// <exception cref="ArgumentOutOfRangeException">A value is outside the range the scene schema declares; nothing changes.</exception>
    /// <exception cref="ArgumentException">The values break a light rule; nothing changes.</exception>
    public override void Hydrate(ComponentData data)
    {
        if (data is PointLightData candidate)
        {
            PropertyChecks.Require(SceneSchema.PointLight.LuminousFluxLumens, candidate.LuminousFluxLumens);
            PropertyChecks.Require(SceneSchema.PointLight.Range, candidate.Range);
            PropertyChecks.Require(SceneSchema.PointLight.SourceRadius, candidate.SourceRadius);
        }

        base.Hydrate(data);
        if (data is not PointLightData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.LuminousFluxLumens = light.LuminousFluxLumens;
            this.Range = light.Range;
            this.SourceRadius = light.SourceRadius;
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new PointLightData
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
        };
}
