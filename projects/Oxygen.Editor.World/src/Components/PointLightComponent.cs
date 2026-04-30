// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

/// <summary>
/// Authored point light component.
/// </summary>
public sealed partial class PointLightComponent : LightComponent
{
    private float luminousFluxLumens = 800f;
    private float range = 10f;
    private float sourceRadius;
    private float decayExponent = 2f;

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
    /// Gets or sets luminous flux, in lumens.
    /// </summary>
    public float LuminousFluxLumens
    {
        get => this.luminousFluxLumens;
        set => _ = this.SetProperty(ref this.luminousFluxLumens, value);
    }

    /// <summary>
    /// Gets or sets the influence range, in meters.
    /// </summary>
    public float Range
    {
        get => this.range;
        set => _ = this.SetProperty(ref this.range, value);
    }

    /// <summary>
    /// Gets or sets the source radius, in meters.
    /// </summary>
    public float SourceRadius
    {
        get => this.sourceRadius;
        set => _ = this.SetProperty(ref this.sourceRadius, value);
    }

    /// <summary>
    /// Gets or sets the attenuation decay exponent.
    /// </summary>
    public float DecayExponent
    {
        get => this.decayExponent;
        set => _ = this.SetProperty(ref this.decayExponent, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
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
            this.DecayExponent = light.DecayExponent;
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
            Mobility = this.Mobility,
            CastsShadows = this.CastsShadows,
            Shadow = this.DehydrateShadow(),
            ExposureCompensation = this.ExposureCompensation,
            LuminousFluxLumens = this.LuminousFluxLumens,
            Range = this.Range,
            SourceRadius = this.SourceRadius,
            DecayExponent = this.DecayExponent,
        };
}
