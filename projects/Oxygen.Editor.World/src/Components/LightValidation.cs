// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The rules that relate several values of a light, and the light ownership of a scene; the range of each single value
/// is decided by <see cref="PropertyChecks"/>.
/// </summary>
/// <remarks>
/// The rules take values that are each in their range: the scene model holds only such values, and loading checks each
/// one before the rules.
/// </remarks>
public static class LightValidation
{
    private const double MinimumNormal = 1.1754943508222875e-38;

    /// <summary>Checks the rules that relate the values of one light.</summary>
    /// <param name="light">The light's values, each in its range.</param>
    /// <returns>The first rule the values break, or <see langword="null"/> when they keep every rule.</returns>
    public static LightRuleViolation? Validate(LightComponentData light)
    {
        ArgumentNullException.ThrowIfNull(light);
        var schema = LightSchema.Of(light);
        if (light is DirectionalLightData directional)
        {
            return ValidateCascades(directional)
                ?? ValidateRepresentable(light, schema, new(directional.IntensityLux, SolidAngle: 1, [SceneSchema.DirectionalLight.IntensityLux.Id]));
        }

        return ValidateLocal(light, out var emission) ?? ValidateRepresentable(light, schema, emission);
    }

    /// <summary>Validates light ownership and the light rules across stored nodes, including inactive ones.</summary>
    /// <param name="scene">The scene whose lights are validated.</param>
    /// <returns>The reason the scene's lights are rejected, or <see langword="null"/> when they are valid.</returns>
    public static string? ValidateScene(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var owners = new Dictionary<AtmosphereLightSlot, string>();
        foreach (var node in scene.AllNodes)
        {
            var lights = node.Components.OfType<LightComponent>().ToArray();
            if (lights.Length > 1)
            {
                return $"'{node.Metadata.Name}' has more than one light.";
            }

            if (lights.Length == 0)
            {
                continue;
            }

            var light = (LightComponentData)lights[0].Dehydrate();
            if (Validate(light) is { } violation)
            {
                return $"'{node.Metadata.Name}': {violation.Message}";
            }

            if (light is DirectionalLightData { AtmosphereSlot: not AtmosphereLightSlot.None } directional
                && !owners.TryAdd(directional.AtmosphereSlot, node.Metadata.Name))
            {
                return $"Atmosphere slot {directional.AtmosphereSlot} is already owned by '{owners[directional.AtmosphereSlot]}'.";
            }
        }

        return null;
    }

    /// <summary>Validates light ownership and the light rules of stored lights before replacing a scene during hydration.</summary>
    /// <param name="scene">The scene data whose lights are validated; each value is in its range.</param>
    /// <returns>The reason the scene's lights are rejected, or <see langword="null"/> when they are valid.</returns>
    public static string? ValidateScene(SceneData scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var owners = new Dictionary<AtmosphereLightSlot, string>();
        var pending = new Stack<SceneNodeData>(scene.RootNodes.Reverse());
        while (pending.TryPop(out var node))
        {
            var lights = node.Components.OfType<LightComponentData>().ToArray();
            if (lights.Length > 1)
            {
                return $"'{node.Name}' has more than one light.";
            }

            foreach (var light in lights)
            {
                if (Validate(light) is { } violation)
                {
                    return $"'{node.Name}': {violation.Message}";
                }

                if (light is DirectionalLightData { AtmosphereSlot: not AtmosphereLightSlot.None } directional
                    && !owners.TryAdd(directional.AtmosphereSlot, node.Name))
                {
                    return $"Atmosphere slot {directional.AtmosphereSlot} is already owned by '{owners[directional.AtmosphereSlot]}'.";
                }
            }

            foreach (var child in node.Children.Reverse())
            {
                pending.Push(child);
            }
        }

        return null;
    }

    private static LightRuleViolation? ValidateCascades(DirectionalLightData directional)
    {
        float previous = 0;
        for (var i = 0; i < directional.CascadeCount; ++i)
        {
            var distance = directional.CascadeDistances[i];
            if (distance <= previous)
            {
                return new(
                    LightRule.CascadeDistancesNotIncreasing,
                    [
                        SceneSchema.DirectionalLight.CascadeCount.Id,
                        SceneSchema.DirectionalLight.CascadeDistance0.Id,
                        SceneSchema.DirectionalLight.CascadeDistance1.Id,
                        SceneSchema.DirectionalLight.CascadeDistance2.Id,
                        SceneSchema.DirectionalLight.CascadeDistance3.Id,
                    ]);
            }

            previous = distance;
        }

        return null;
    }

    private static LightRuleViolation? ValidateLocal(LightComponentData light, out Emission emission)
    {
        var (range, radius, flux, rangeId, radiusId, fluxId) = light switch
        {
            PointLightData point => (point.Range, point.SourceRadius, point.LuminousFluxLumens, SceneSchema.PointLight.Range.Id, SceneSchema.PointLight.SourceRadius.Id, SceneSchema.PointLight.LuminousFluxLumens.Id),
            SpotLightData spot => (spot.Range, spot.SourceRadius, spot.LuminousFluxLumens, SceneSchema.SpotLight.Range.Id, SceneSchema.SpotLight.SourceRadius.Id, SceneSchema.SpotLight.LuminousFluxLumens.Id),
            _ => throw new ArgumentException($"A {light.GetType().Name} is not a local light.", nameof(light)),
        };
        emission = new(flux, SolidAngle: 4 * Math.PI, [fluxId]);
        if ((double)range + radius > float.MaxValue)
        {
            return new(LightRule.ExtentNotRepresentable, [rangeId, radiusId]);
        }

        if (1.0 / range is < MinimumNormal or > float.MaxValue)
        {
            return new(LightRule.InverseRangeNotRepresentable, [rangeId]);
        }

        if (light is not SpotLightData spotLight)
        {
            return null;
        }

        var violation = ValidateSpotCone(spotLight, out var coneFraction);
        emission = emission with
        {
            SolidAngle = emission.SolidAngle * coneFraction,
            Properties = [fluxId, SceneSchema.SpotLight.InnerConeAngleRadians.Id, SceneSchema.SpotLight.OuterConeAngleRadians.Id],
        };
        return violation;
    }

    private static LightRuleViolation? ValidateSpotCone(SpotLightData spot, out double coneFraction)
    {
        coneFraction = 1;
        var inner = spot.InnerConeAngleRadians;
        var outer = spot.OuterConeAngleRadians;
        PropertyId[] cone = [SceneSchema.SpotLight.InnerConeAngleRadians.Id, SceneSchema.SpotLight.OuterConeAngleRadians.Id];
        if (outer <= 0 || inner > outer || (inner == outer && outer == MathF.PI / 2))
        {
            return new(LightRule.SpotConeInvalid, cone);
        }

        var innerSineSquared = inner == MathF.PI / 2 ? 0.5 : Math.Pow(Math.Sin(inner / 2.0), 2);
        var outerSineSquared = outer == MathF.PI / 2 ? 0.5 : Math.Pow(Math.Sin(outer / 2.0), 2);
        var innerCosine = (float)(1 - (2 * innerSineSquared));
        var outerCosine = (float)(1 - (2 * outerSineSquared));
        if (outerCosine >= 1 || (inner != outer && innerCosine <= outerCosine))
        {
            return new(LightRule.SpotConeNotRepresentable, cone);
        }

        coneFraction = innerSineSquared + ((outerSineSquared - innerSineSquared) / 3);
        return null;
    }

    private static LightRuleViolation? ValidateRepresentable(LightComponentData light, LightSchema schema, Emission emission)
    {
        PropertyId[] intensity = [.. emission.Properties, schema.ColorId.Id, schema.ExposureCompensationId.Id];
        for (var channel = 0; channel < 3; ++channel)
        {
            if (emission.Strength == 0 || light.Color[channel] == 0)
            {
                continue;
            }

            var logIntensity = Math.Log2(emission.Strength * (double)light.Color[channel] / emission.SolidAngle) + light.ExposureCompensation;
            if (!double.IsFinite(logIntensity) || logIntensity < -126 || logIntensity > Math.Log2(float.MaxValue))
            {
                return new(LightRule.IntensityNotRepresentable, intensity);
            }

            if (light is DirectionalLightData disk && disk.AngularSizeRadians > 0)
            {
                var sine = Math.Sin(disk.AngularSizeRadians / 2.0);
                var radiance = Math.Pow(2, logIntensity) * disk.AtmosphereDiskLuminanceScaleRgb[channel] / (Math.PI * sine * sine);
                if (!double.IsFinite(radiance) || radiance > float.MaxValue || (radiance > 0 && radiance < MinimumNormal))
                {
                    return new(
                        LightRule.DiskRadianceNotRepresentable,
                        [.. intensity, SceneSchema.DirectionalLight.AngularSizeRadians.Id, SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb.Id]);
                }
            }
        }

        return null;
    }

    // What a light emits, the solid angle it spreads over, and the properties that decide both.
    [StructLayout(LayoutKind.Auto)]
    private readonly record struct Emission(float Strength, double SolidAngle, IReadOnlyList<PropertyId> Properties);
}
