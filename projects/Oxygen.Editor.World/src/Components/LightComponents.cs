// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

public abstract partial class LightComponent : GameComponent
{
    private bool affectsWorld = true;
    private Vector3 color = Vector3.One;
    private bool castsShadows;
    private float exposureCompensation;

    public bool AffectsWorld
    {
        get => this.affectsWorld;
        set => _ = this.SetProperty(ref this.affectsWorld, value);
    }

    public Vector3 Color
    {
        get => this.color;
        set => _ = this.SetProperty(ref this.color, value);
    }

    public bool CastsShadows
    {
        get => this.castsShadows;
        set => _ = this.SetProperty(ref this.castsShadows, value);
    }

    public float ExposureCompensation
    {
        get => this.exposureCompensation;
        set => _ = this.SetProperty(ref this.exposureCompensation, value);
    }

    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not LightComponentData light)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.AffectsWorld = light.AffectsWorld;
            this.Color = light.Color;
            this.CastsShadows = light.CastsShadows;
            this.ExposureCompensation = light.ExposureCompensation;
        }
    }

}

public sealed partial class DirectionalLightComponent : LightComponent
{
    private float intensityLux = DefaultIntensityLux;
    private float angularSizeRadians = DefaultAngularSizeRadians;
    private bool environmentContribution = true;
    private bool isSunLight = true;

    public const float DefaultIntensityLux = 100_000f;

    public const float DefaultAngularSizeRadians = 0.00935f;

    public static readonly Quaternion DefaultLocalRotation = Quaternion.CreateFromYawPitchRoll(0f, 0.7853982f, 0f);

    static DirectionalLightComponent()
    {
        Register<DirectionalLightData>(d =>
        {
            var c = new DirectionalLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    public float IntensityLux
    {
        get => this.intensityLux;
        set => _ = this.SetProperty(ref this.intensityLux, value);
    }

    public float AngularSizeRadians
    {
        get => this.angularSizeRadians;
        set => _ = this.SetProperty(ref this.angularSizeRadians, value);
    }

    public bool EnvironmentContribution
    {
        get => this.environmentContribution;
        set => _ = this.SetProperty(ref this.environmentContribution, value);
    }

    public bool IsSunLight
    {
        get => this.isSunLight;
        set => _ = this.SetProperty(ref this.isSunLight, value);
    }

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
        }
    }

    public override ComponentData Dehydrate()
        => new DirectionalLightData
        {
            Id = this.Id,
            Name = this.Name,
            AffectsWorld = this.AffectsWorld,
            Color = this.Color,
            CastsShadows = this.CastsShadows,
            ExposureCompensation = this.ExposureCompensation,
            IntensityLux = this.IntensityLux,
            AngularSizeRadians = this.AngularSizeRadians,
            EnvironmentContribution = this.EnvironmentContribution,
            IsSunLight = this.IsSunLight,
        };
}

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

    public float LuminousFluxLumens
    {
        get => this.luminousFluxLumens;
        set => _ = this.SetProperty(ref this.luminousFluxLumens, value);
    }

    public float Range
    {
        get => this.range;
        set => _ = this.SetProperty(ref this.range, value);
    }

    public float SourceRadius
    {
        get => this.sourceRadius;
        set => _ = this.SetProperty(ref this.sourceRadius, value);
    }

    public float DecayExponent
    {
        get => this.decayExponent;
        set => _ = this.SetProperty(ref this.decayExponent, value);
    }

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

    public override ComponentData Dehydrate()
        => new PointLightData
        {
            Id = this.Id,
            Name = this.Name,
            AffectsWorld = this.AffectsWorld,
            Color = this.Color,
            CastsShadows = this.CastsShadows,
            ExposureCompensation = this.ExposureCompensation,
            LuminousFluxLumens = this.LuminousFluxLumens,
            Range = this.Range,
            SourceRadius = this.SourceRadius,
            DecayExponent = this.DecayExponent,
        };
}

public sealed partial class SpotLightComponent : LightComponent
{
    private float luminousFluxLumens = 800f;
    private float range = 10f;
    private float sourceRadius;
    private float decayExponent = 2f;
    private float innerConeAngleRadians = 0.4f;
    private float outerConeAngleRadians = 0.6f;

    static SpotLightComponent()
    {
        Register<SpotLightData>(d =>
        {
            var c = new SpotLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    public float LuminousFluxLumens
    {
        get => this.luminousFluxLumens;
        set => _ = this.SetProperty(ref this.luminousFluxLumens, value);
    }

    public float Range
    {
        get => this.range;
        set => _ = this.SetProperty(ref this.range, value);
    }

    public float SourceRadius
    {
        get => this.sourceRadius;
        set => _ = this.SetProperty(ref this.sourceRadius, value);
    }

    public float DecayExponent
    {
        get => this.decayExponent;
        set => _ = this.SetProperty(ref this.decayExponent, value);
    }

    public float InnerConeAngleRadians
    {
        get => this.innerConeAngleRadians;
        set => _ = this.SetProperty(ref this.innerConeAngleRadians, value);
    }

    public float OuterConeAngleRadians
    {
        get => this.outerConeAngleRadians;
        set => _ = this.SetProperty(ref this.outerConeAngleRadians, value);
    }

    public override void Hydrate(ComponentData data)
    {
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
            this.DecayExponent = light.DecayExponent;
            this.InnerConeAngleRadians = light.InnerConeAngleRadians;
            this.OuterConeAngleRadians = light.OuterConeAngleRadians;
        }
    }

    public override ComponentData Dehydrate()
        => new SpotLightData
        {
            Id = this.Id,
            Name = this.Name,
            AffectsWorld = this.AffectsWorld,
            Color = this.Color,
            CastsShadows = this.CastsShadows,
            ExposureCompensation = this.ExposureCompensation,
            LuminousFluxLumens = this.LuminousFluxLumens,
            Range = this.Range,
            SourceRadius = this.SourceRadius,
            DecayExponent = this.DecayExponent,
            InnerConeAngleRadians = this.InnerConeAngleRadians,
            OuterConeAngleRadians = this.OuterConeAngleRadians,
        };
}
