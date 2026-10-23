// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The scene schema's identities and creation values of the properties every light has, for one light kind: each kind
/// declares its own, so a light reads the identities, ranges and defaults of its kind.
/// </summary>
/// <param name="ColorId">The color property.</param>
/// <param name="ExposureCompensationId">The exposure compensation property.</param>
/// <param name="ShadowBiasId">The shadow depth bias property.</param>
/// <param name="ShadowNormalBiasId">The shadow normal bias property.</param>
/// <param name="ShadowResolutionHintId">The shadow resolution hint property.</param>
/// <param name="AffectsWorld">The creation value of the world contribution flag.</param>
/// <param name="Color">The creation value of the color.</param>
/// <param name="CastsShadows">The creation value of the shadow casting flag.</param>
/// <param name="ExposureCompensation">The creation value of the exposure compensation.</param>
/// <param name="ShadowBias">The creation value of the shadow depth bias.</param>
/// <param name="ShadowNormalBias">The creation value of the shadow normal bias.</param>
/// <param name="ContactShadows">The creation value of the contact shadows flag.</param>
/// <param name="ShadowResolutionHint">The creation value of the shadow resolution hint.</param>
internal sealed record LightSchema(
    PropertyId<Vector3> ColorId,
    PropertyId<float> ExposureCompensationId,
    PropertyId<float> ShadowBiasId,
    PropertyId<float> ShadowNormalBiasId,
    PropertyId<ShadowResolutionHint> ShadowResolutionHintId,
    bool AffectsWorld,
    Vector3 Color,
    bool CastsShadows,
    float ExposureCompensation,
    float ShadowBias,
    float ShadowNormalBias,
    bool ContactShadows,
    ShadowResolutionHint ShadowResolutionHint)
{
    /// <summary>Gets the schema of a directional light.</summary>
    public static LightSchema Directional { get; } = new(
        SceneSchema.DirectionalLight.Color,
        SceneSchema.DirectionalLight.ExposureCompensation,
        SceneSchema.DirectionalLight.ShadowBias,
        SceneSchema.DirectionalLight.ShadowNormalBias,
        SceneSchema.DirectionalLight.ShadowResolutionHint,
        SceneSchema.DirectionalLight.Defaults.AffectsWorld,
        SceneSchema.DirectionalLight.Defaults.Color,
        SceneSchema.DirectionalLight.Defaults.CastsShadows,
        SceneSchema.DirectionalLight.Defaults.ExposureCompensation,
        SceneSchema.DirectionalLight.Defaults.ShadowBias,
        SceneSchema.DirectionalLight.Defaults.ShadowNormalBias,
        SceneSchema.DirectionalLight.Defaults.ContactShadows,
        SceneSchema.DirectionalLight.Defaults.ShadowResolutionHint);

    /// <summary>Gets the schema of a point light.</summary>
    public static LightSchema Point { get; } = new(
        SceneSchema.PointLight.Color,
        SceneSchema.PointLight.ExposureCompensation,
        SceneSchema.PointLight.ShadowBias,
        SceneSchema.PointLight.ShadowNormalBias,
        SceneSchema.PointLight.ShadowResolutionHint,
        SceneSchema.PointLight.Defaults.AffectsWorld,
        SceneSchema.PointLight.Defaults.Color,
        SceneSchema.PointLight.Defaults.CastsShadows,
        SceneSchema.PointLight.Defaults.ExposureCompensation,
        SceneSchema.PointLight.Defaults.ShadowBias,
        SceneSchema.PointLight.Defaults.ShadowNormalBias,
        SceneSchema.PointLight.Defaults.ContactShadows,
        SceneSchema.PointLight.Defaults.ShadowResolutionHint);

    /// <summary>Gets the schema of a spot light.</summary>
    public static LightSchema Spot { get; } = new(
        SceneSchema.SpotLight.Color,
        SceneSchema.SpotLight.ExposureCompensation,
        SceneSchema.SpotLight.ShadowBias,
        SceneSchema.SpotLight.ShadowNormalBias,
        SceneSchema.SpotLight.ShadowResolutionHint,
        SceneSchema.SpotLight.Defaults.AffectsWorld,
        SceneSchema.SpotLight.Defaults.Color,
        SceneSchema.SpotLight.Defaults.CastsShadows,
        SceneSchema.SpotLight.Defaults.ExposureCompensation,
        SceneSchema.SpotLight.Defaults.ShadowBias,
        SceneSchema.SpotLight.Defaults.ShadowNormalBias,
        SceneSchema.SpotLight.Defaults.ContactShadows,
        SceneSchema.SpotLight.Defaults.ShadowResolutionHint);

    /// <summary>Gets the schema of a light kind.</summary>
    /// <param name="light">The light's data.</param>
    /// <returns>The schema of the light's kind.</returns>
    /// <exception cref="ArgumentException">The light is not a directional, point or spot light.</exception>
    public static LightSchema Of(LightComponentData light)
    {
        ArgumentNullException.ThrowIfNull(light);
        return light switch
        {
            DirectionalLightData => Directional,
            PointLightData => Point,
            SpotLightData => Spot,
            _ => throw new ArgumentException($"A {light.GetType().Name} is not a known light kind.", nameof(light)),
        };
    }
}
