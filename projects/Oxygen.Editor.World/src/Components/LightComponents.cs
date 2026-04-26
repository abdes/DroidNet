// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World;

public abstract partial class LightComponent : GameComponent
{
    public bool AffectsWorld { get; set; } = true;

    public Vector3 Color { get; set; } = Vector3.One;

    public bool CastsShadows { get; set; }

    public float ExposureCompensation { get; set; }

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

    public float IntensityLux { get; set; } = DefaultIntensityLux;

    public float AngularSizeRadians { get; set; } = DefaultAngularSizeRadians;

    public bool EnvironmentContribution { get; set; } = true;

    public bool IsSunLight { get; set; } = true;

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
    static PointLightComponent()
    {
        Register<PointLightData>(d =>
        {
            var c = new PointLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    public float LuminousFluxLumens { get; set; } = 800f;

    public float Range { get; set; } = 10f;

    public float SourceRadius { get; set; }

    public float DecayExponent { get; set; } = 2f;

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
    static SpotLightComponent()
    {
        Register<SpotLightData>(d =>
        {
            var c = new SpotLightComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    public float LuminousFluxLumens { get; set; } = 800f;

    public float Range { get; set; } = 10f;

    public float SourceRadius { get; set; }

    public float DecayExponent { get; set; } = 2f;

    public float InnerConeAngleRadians { get; set; } = 0.4f;

    public float OuterConeAngleRadians { get; set; } = 0.6f;

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
