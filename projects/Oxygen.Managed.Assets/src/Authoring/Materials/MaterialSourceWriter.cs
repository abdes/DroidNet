// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>Writes canonical engine material source while preserving unedited fields.</summary>
public static class MaterialSourceWriter
{
    /// <summary>Writes the source using the same descriptor consumed by native cooking.</summary>
    /// <param name="output">The destination stream.</param>
    /// <param name="material">The immutable source snapshot.</param>
    /// <param name="identity">The material's identity, which its references are spelled from.</param>
    /// <exception cref="AssetFormatException">The source does not make a valid descriptor.</exception>
    public static void Write(Stream output, MaterialSource material, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(output);
        JsonSerializer.Serialize(output, ToJson(material, identity), Serialization.Options);
    }

    /// <summary>Creates an independently owned, schema-validated canonical descriptor.</summary>
    /// <param name="material">The source snapshot.</param>
    /// <param name="identity">The material's identity, which its references are spelled from.</param>
    /// <returns>The descriptor, including preserved native fields. A binding field or the occlusion mode that holds the
    /// engine's default is not written.</returns>
    /// <exception cref="AssetFormatException">The source does not make a valid descriptor.</exception>
    public static JsonObject ToJson(MaterialSource material, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(material);
        ArgumentNullException.ThrowIfNull(identity);
        var descriptor = material.Descriptor is { } saved
            ? JsonNode.Parse(saved.GetRawText())!.AsObject() : new JsonObject();
        if (material.Name is { Length: > 0 } name)
        {
            descriptor["name"] = name;
        }
        else
        {
            _ = descriptor.Remove("name");
        }

        var alphaMode = material.AlphaMode switch
        {
            MaterialAlphaMode.Opaque => "opaque",
            MaterialAlphaMode.Mask => "masked",
            MaterialAlphaMode.Blend => "blended",
            _ => throw new AssetFormatException(new AssetProblem(DiagnosticSeverity.Error, MaterialSourceCodes.SchemaMismatch, $"The material alpha mode {material.AlphaMode} is not one the engine knows.")),
        };
        if (material.Descriptor is not { } original || MaterialSource.ResolveAlphaMode(original) != material.AlphaMode)
        {
            descriptor["alpha_mode"] = alphaMode;
            if (descriptor["domain"]?.GetValue<string>() is "opaque" or "masked" or "alpha_blended")
            {
                descriptor["domain"] = material.AlphaMode == MaterialAlphaMode.Blend ? "alpha_blended" : alphaMode;
            }
        }

        var parameters = descriptor["parameters"] as JsonObject ?? new JsonObject();
        if (parameters.Parent is null)
        {
            descriptor["parameters"] = parameters;
        }

        var pbr = material.PbrMetallicRoughness;
        parameters["base_color"] = new JsonArray(pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA);
        parameters["metalness"] = pbr.MetallicFactor;
        var originalParameters = material.Descriptor is { } savedDescriptor && savedDescriptor.TryGetProperty("parameters", out var savedParameters)
            ? savedParameters : default;
        if (material.Descriptor is null || pbr.RoughnessFactor != MaterialSource.ResolveRoughness(originalParameters))
        {
            parameters["roughness"] = MaterialSource.UsesGlossiness(originalParameters) ? 1.0f - pbr.RoughnessFactor : pbr.RoughnessFactor;
        }

        parameters["double_sided"] = material.DoubleSided;
        parameters["alpha_cutoff"] = material.AlphaCutoff;
        parameters["emissive_color"] = new JsonArray(material.EmissiveColor.X, material.EmissiveColor.Y, material.EmissiveColor.Z);
        parameters["emissive_intensity"] = material.EmissiveIntensity;
        if (material.TextureReferences.ContainsKey("normal") || ShouldWriteSetting(material, "normal_scale", material.NormalScale, 1.0f))
        {
            parameters["normal_scale"] = material.NormalScale;
        }

        if (material.TextureReferences.ContainsKey("ambient_occlusion") || ShouldWriteSetting(material, "ambient_occlusion", material.OcclusionStrength, 1.0f))
        {
            parameters["ambient_occlusion"] = material.OcclusionStrength;
        }

        ApplyTextureBindings(descriptor, material.TextureBindings, identity);
        Set(descriptor, "ambient_occlusion_mode", material.AmbientOcclusionMode == MaterialAmbientOcclusionMode.Strength ? "strength" : null);
        Set(descriptor, "procedural_grid", material.ProceduralGrid is { } grid ? ToJson(grid) : null);

        MaterialSourceReader.Validate(JsonSerializer.SerializeToElement(descriptor), identity);
        return descriptor;
    }

    private static bool ShouldWriteSetting(MaterialSource material, string setting, float value, float defaultValue)
    {
        if (material.Descriptor is not { } descriptor
            || !descriptor.TryGetProperty("parameters", out var parameters)
            || !parameters.TryGetProperty(setting, out var original))
        {
            return value != defaultValue;
        }

        return original.GetSingle() != value;
    }

    private static void ApplyTextureBindings(JsonObject descriptor, IReadOnlyDictionary<string, MaterialTextureBinding> bindings, AssetPath identity)
    {
        var textures = new JsonObject();
        foreach (var channel in MaterialSource.TextureChannels)
        {
            if (bindings.TryGetValue(channel, out var binding))
            {
                textures[channel] = ToJson(binding, identity);
            }
        }

        Set(descriptor, "textures", textures.Count == 0 ? null : textures);
    }

    private static JsonObject ToJson(MaterialTextureBinding binding, AssetPath identity)
    {
        var json = new JsonObject { ["virtual_path"] = binding.Texture.ToReference(identity) };
        Set(json, "uv_set", binding.UvSet == 0 ? null : binding.UvSet);
        Set(json, "uv_transform", binding.UvTransform == MaterialUvTransform.Identity ? null : ToJson(binding.UvTransform));
        Set(json, "sampler", binding.Sampler == MaterialTextureSampler.Default ? null : ToJson(binding.Sampler));
        return json;
    }

    private static JsonObject ToJson(MaterialUvTransform transform)
    {
        var json = new JsonObject();
        Set(json, "scale", transform.Scale == Vector2.One ? null : Array(transform.Scale));
        Set(json, "offset", transform.Offset == Vector2.Zero ? null : Array(transform.Offset));
        Set(json, "rotation_radians", transform.RotationRadians == 0.0f ? null : transform.RotationRadians);
        return json;
    }

    private static JsonObject ToJson(MaterialTextureSampler sampler)
    {
        var json = new JsonObject();
        Set(json, "address_u", sampler.AddressU == MaterialTextureSampler.Default.AddressU ? null : Name(sampler.AddressU));
        Set(json, "address_v", sampler.AddressV == MaterialTextureSampler.Default.AddressV ? null : Name(sampler.AddressV));
        Set(json, "mag_filter", sampler.MagFilter == MaterialTextureSampler.Default.MagFilter ? null : Name(sampler.MagFilter));
        Set(json, "min_filter", sampler.MinFilter == MaterialTextureSampler.Default.MinFilter ? null : Name(sampler.MinFilter));
        Set(json, "mip_filter", sampler.MipFilter == MaterialTextureSampler.Default.MipFilter ? null : Name(sampler.MipFilter));
        return json;
    }

    private static JsonObject ToJson(MaterialProceduralGrid grid)
    {
        var json = new JsonObject();
        Set(json, "spacing", grid.Spacing is { } spacing ? Array(spacing) : null);
        Set(json, "major_every", grid.MajorEvery);
        Set(json, "line_thickness", grid.LineThickness);
        Set(json, "major_thickness", grid.MajorThickness);
        Set(json, "axis_thickness", grid.AxisThickness);
        Set(json, "fade_start", grid.FadeStart);
        Set(json, "fade_end", grid.FadeEnd);
        Set(json, "minor_color", grid.MinorColor is { } minor ? Array(minor) : null);
        Set(json, "major_color", grid.MajorColor is { } major ? Array(major) : null);
        Set(json, "axis_color_x", grid.AxisColorX is { } axisX ? Array(axisX) : null);
        Set(json, "axis_color_y", grid.AxisColorY is { } axisY ? Array(axisY) : null);
        Set(json, "origin_color", grid.OriginColor is { } origin ? Array(origin) : null);
        return json;
    }

    private static JsonArray Array(Vector2 value) => new(value.X, value.Y);

    private static JsonArray Array(Vector4 value) => new(value.X, value.Y, value.Z, value.W);

    private static string Name<T>(T value)
        where T : struct, Enum
        => value.ToString().ToLowerInvariant();

    private static void Set(JsonObject parent, string name, JsonNode? value)
    {
        if (value is null)
        {
            _ = parent.Remove(name);
        }
        else
        {
            parent[name] = value;
        }
    }
}
