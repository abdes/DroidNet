// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Runtime.InteropServices;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>Validates complete authored light values before hydration, edits or cooking.</summary>
public static class LightValidation
{
    private const double MinimumNormal = 1.1754943508222875e-38;

    /// <summary>Returns the rejected field and reason, or null for a valid candidate.</summary>
    /// <param name="light">The candidate light values.</param>
    /// <returns>The reason the light is rejected, or <see langword="null"/> when it is valid.</returns>
    public static string? Validate(LightComponentData light)
    {
        ArgumentNullException.ThrowIfNull(light);
        if (ValidateCommon(light) is { } common)
        {
            return common;
        }

        string? error;
        Emission emission;
        if (light is DirectionalLightData directional)
        {
            error = ValidateDirectional(directional);
            emission = new(directional.IntensityLux, SolidAngle: 1);
        }
        else
        {
            error = ValidateLocal(light, out emission);
        }

        return error ?? ValidateRepresentable(light, emission);
    }

    /// <summary>Validates light ownership and values across stored nodes, including inactive ones.</summary>
    /// <param name="scene">The scene whose lights are validated.</param>
    /// <param name="candidates">Directional light values to validate in place of the stored ones, by node.</param>
    /// <returns>The reason the scene's lights are rejected, or <see langword="null"/> when they are valid.</returns>
    public static string? ValidateScene(Scene scene, IReadOnlyDictionary<NodeId, DirectionalLightData>? candidates = null)
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

            var light = candidates is not null && candidates.TryGetValue(node.Id, out var candidate)
                ? candidate : (LightComponentData)lights[0].Dehydrate();
            if (Validate(light) is { } error)
            {
                return $"'{node.Metadata.Name}': {error}";
            }

            if (light is DirectionalLightData { AtmosphereSlot: not AtmosphereLightSlot.None } directional
                && !owners.TryAdd(directional.AtmosphereSlot, node.Metadata.Name))
            {
                return $"Atmosphere slot {directional.AtmosphereSlot} is already owned by '{owners[directional.AtmosphereSlot]}'.";
            }
        }

        return null;
    }

    /// <summary>Validates stored lights before replacing a scene during hydration.</summary>
    /// <param name="scene">The scene data whose lights are validated.</param>
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
                if (Validate(light) is { } error)
                {
                    return $"'{node.Name}': {error}";
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

    private static string? ValidateCommon(LightComponentData light)
    {
        if (!Nonnegative(light.Color))
        {
            return "Color must be finite and nonnegative.";
        }

        if (!float.IsFinite(light.ExposureCompensation))
        {
            return "Exposure compensation must be finite.";
        }

        var shadow = light.Shadow ?? new();
        return !Nonnegative(shadow.Bias) || !Nonnegative(shadow.NormalBias)
            ? "Shadow biases must be finite and nonnegative."
            : Enum.IsDefined(shadow.ResolutionHint) ? null : "Unknown shadow resolution hint.";
    }

    private static string? ValidateDirectional(DirectionalLightData directional)
        => !Enum.IsDefined(directional.AtmosphereSlot) ? "Unknown atmosphere light slot."
            : !Nonnegative(directional.AngularSizeRadians) || directional.AngularSizeRadians > MathF.PI ? "Angular diameter must be in [0, pi]."
            : !Nonnegative(directional.AtmosphereDiskLuminanceScaleRgb) ? "Disk scale must be finite and nonnegative."
            : ValidateCascades(directional);

    private static string? ValidateCascades(DirectionalLightData directional)
    {
        if (directional.CascadeCount is < 1 or > 4 || !Enum.IsDefined(directional.SplitMode))
        {
            return "Invalid cascade count or split mode.";
        }

        if (!Nonnegative(directional.MaxShadowDistance) || directional.MaxShadowDistance == 0)
        {
            return "Shadow distance must be positive.";
        }

        if (!float.IsFinite(directional.DistributionExponent) || directional.DistributionExponent < 1)
        {
            return "Distribution exponent must be at least one.";
        }

        if (!Fraction(directional.TransitionFraction) || !Fraction(directional.DistanceFadeoutFraction))
        {
            return "Shadow fade fractions must be in [0, 1].";
        }

        float previous = 0;
        for (var i = 0; i < 4; ++i)
        {
            var distance = directional.CascadeDistances[i];
            if (!float.IsFinite(distance) || (i < directional.CascadeCount && distance <= previous))
            {
                return "Active cascade distances must be positive and increasing; inactive entries must be finite.";
            }

            previous = distance;
        }

        return null;
    }

    private static string? ValidateLocal(LightComponentData light, out Emission emission)
    {
        var (range, radius, flux) = light switch
        {
            PointLightData point => (point.Range, point.SourceRadius, point.LuminousFluxLumens),
            SpotLightData spot => (spot.Range, spot.SourceRadius, spot.LuminousFluxLumens),
            _ => (float.NaN, 0f, 0f),
        };
        emission = new(flux, SolidAngle: 4 * Math.PI);
        if (!Nonnegative(range) || !Nonnegative(radius) || (double)range + radius > float.MaxValue)
        {
            return "Local range and radius must be finite and nonnegative.";
        }

        if (range > 0 && (1.0 / range < MinimumNormal || 1.0 / range > float.MaxValue))
        {
            return "Inverse light range is not representable.";
        }

        if (light is not SpotLightData spotLight)
        {
            return null;
        }

        var error = ValidateSpotCone(spotLight, out var coneFraction);
        emission = emission with { SolidAngle = emission.SolidAngle * coneFraction };
        return error;
    }

    private static string? ValidateSpotCone(SpotLightData spot, out double coneFraction)
    {
        coneFraction = 1;
        var inner = spot.InnerConeAngleRadians;
        var outer = spot.OuterConeAngleRadians;
        if (!Nonnegative(inner) || !float.IsFinite(outer) || outer <= 0 || inner > outer || outer > MathF.PI / 2
            || (inner == outer && outer == MathF.PI / 2))
        {
            return "Invalid spot cone pair.";
        }

        var innerSineSquared = inner == MathF.PI / 2 ? 0.5 : Math.Pow(Math.Sin(inner / 2.0), 2);
        var outerSineSquared = outer == MathF.PI / 2 ? 0.5 : Math.Pow(Math.Sin(outer / 2.0), 2);
        var innerCosine = (float)(1 - (2 * innerSineSquared));
        var outerCosine = (float)(1 - (2 * outerSineSquared));
        if (outerCosine >= 1 || (inner != outer && innerCosine <= outerCosine))
        {
            return "Spot cone is not representable by the renderer.";
        }

        coneFraction = innerSineSquared + ((outerSineSquared - innerSineSquared) / 3);
        return null;
    }

    private static string? ValidateRepresentable(LightComponentData light, Emission emission)
    {
        if (!Nonnegative(emission.Strength))
        {
            return "Light intensity must be finite and nonnegative.";
        }

        for (var channel = 0; channel < 3; ++channel)
        {
            if (emission.Strength == 0 || light.Color[channel] == 0)
            {
                continue;
            }

            var logIntensity = Math.Log2(emission.Strength * (double)light.Color[channel] / emission.SolidAngle) + light.ExposureCompensation;
            if (!double.IsFinite(logIntensity) || logIntensity < -126 || logIntensity > Math.Log2(float.MaxValue))
            {
                return "Resolved light intensity is not representable.";
            }

            if (light is DirectionalLightData disk && disk.AngularSizeRadians > 0)
            {
                var sine = Math.Sin(disk.AngularSizeRadians / 2.0);
                var radiance = Math.Pow(2, logIntensity) * disk.AtmosphereDiskLuminanceScaleRgb[channel] / (Math.PI * sine * sine);
                if (!double.IsFinite(radiance) || radiance > float.MaxValue || (radiance > 0 && radiance < MinimumNormal))
                {
                    return "Resolved disk radiance is not representable.";
                }
            }
        }

        return null;
    }

    private static bool Nonnegative(float value) => float.IsFinite(value) && value >= 0;

    private static bool Nonnegative(Vector3 value) => Nonnegative(value.X) && Nonnegative(value.Y) && Nonnegative(value.Z);

    private static bool Fraction(float value) => Nonnegative(value) && value <= 1;

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct Emission(float Strength, double SolidAngle);
}
