// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

[JsonDerivedType(typeof(DirectionalLightData), "DirectionalLight")]
[JsonDerivedType(typeof(PointLightData), "PointLight")]
[JsonDerivedType(typeof(SpotLightData), "SpotLight")]
public abstract record LightComponentData : ComponentData
{
    public bool AffectsWorld { get; init; } = true;

    public Vector3 Color { get; init; } = Vector3.One;

    public bool CastsShadows { get; init; }

    public float ExposureCompensation { get; init; }
}

public sealed record DirectionalLightData : LightComponentData
{
    public float IntensityLux { get; init; } = 100_000f;

    public float AngularSizeRadians { get; init; } = 0.00935f;

    public bool EnvironmentContribution { get; init; } = true;

    public bool IsSunLight { get; init; } = true;
}

public sealed record PointLightData : LightComponentData
{
    public float LuminousFluxLumens { get; init; } = 800f;

    public float Range { get; init; } = 10f;

    public float SourceRadius { get; init; }

    public float DecayExponent { get; init; } = 2f;
}

public sealed record SpotLightData : LightComponentData
{
    public float LuminousFluxLumens { get; init; } = 800f;

    public float Range { get; init; } = 10f;

    public float SourceRadius { get; init; }

    public float DecayExponent { get; init; } = 2f;

    public float InnerConeAngleRadians { get; init; } = 0.4f;

    public float OuterConeAngleRadians { get; init; } = 0.6f;
}
