// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Assets.Import.Scenes;

public sealed record SceneSource(
    string Schema,
    string Name,
    List<SceneNodeSource> Nodes);

public sealed record SceneNodeSource(
    string Name,
    Vector3? Translation,
    Quaternion? Rotation,
    Vector3? Scale,
    string? Mesh,
    List<SceneNodeSource>? Children)
{
    public Guid? Id { get; init; }

    public SceneNodeFlagsSource? Flags { get; init; }

    public PerspectiveCameraSource? PerspectiveCamera { get; init; }

    public DirectionalLightSource? DirectionalLight { get; init; }

    public PointLightSource? PointLight { get; init; }

    public SpotLightSource? SpotLight { get; init; }
}

public sealed record SceneNodeFlagsSource(
    bool Visible = true,
    bool Static = false,
    bool CastsShadows = true,
    bool ReceivesShadows = true,
    bool RayCastingSelectable = true,
    bool IgnoreParentTransform = false);

public sealed record PerspectiveCameraSource(
    float FieldOfView,
    float AspectRatio,
    float NearPlane,
    float FarPlane);

public sealed record LightCommonSource(
    bool AffectsWorld,
    float Red,
    float Green,
    float Blue,
    bool CastsShadows,
    float ExposureCompensation);

public sealed record DirectionalLightSource(
    LightCommonSource Common,
    float IntensityLux,
    float AngularSizeRadians,
    bool EnvironmentContribution,
    bool IsSunLight);

public sealed record PointLightSource(
    LightCommonSource Common,
    float LuminousFluxLumens,
    float Range,
    float SourceRadius,
    float DecayExponent);

public sealed record SpotLightSource(
    LightCommonSource Common,
    float LuminousFluxLumens,
    float Range,
    float SourceRadius,
    float DecayExponent,
    float InnerConeAngleRadians,
    float OuterConeAngleRadians);
