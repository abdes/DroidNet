// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>Writes canonical engine material source while preserving unedited fields.</summary>
public static class MaterialSourceWriter
{
    /// <summary>Writes the source using the same descriptor consumed by native cooking.</summary>
    /// <param name="output">The destination stream.</param>
    /// <param name="material">The immutable source snapshot.</param>
    public static void Write(Stream output, MaterialSource material)
    {
        ArgumentNullException.ThrowIfNull(output);
        JsonSerializer.Serialize(output, ToJson(material), Serialization.Options);
    }

    /// <summary>Creates an independently owned, schema-validated canonical descriptor.</summary>
    /// <param name="material">The source snapshot.</param>
    /// <returns>The descriptor, including preserved texture and native fields.</returns>
    public static JsonObject ToJson(MaterialSource material)
    {
        ArgumentNullException.ThrowIfNull(material);
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
            _ => throw new InvalidDataException("Invalid material alpha mode."),
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
        if (material.NormalTexture is not null || ShouldWriteSetting(material, "normal_scale", material.NormalScale, 1.0f))
        {
            parameters["normal_scale"] = material.NormalScale;
        }

        if (material.OcclusionTexture is not null || ShouldWriteSetting(material, "ambient_occlusion", material.OcclusionStrength, 1.0f))
        {
            parameters["ambient_occlusion"] = material.OcclusionStrength;
        }

        ApplyTextureReferences(descriptor, material.TextureReferences);

        MaterialSourceReader.Validate(JsonSerializer.SerializeToElement(descriptor));
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

    private static void ApplyTextureReferences(JsonObject descriptor, IReadOnlyDictionary<string, string> references)
    {
        var textures = descriptor["textures"] is JsonObject original
            ? (JsonObject)original.DeepClone()
            : new JsonObject();
        foreach (var channel in MaterialSource.TextureChannels)
        {
            if (!references.TryGetValue(channel, out var virtualPath))
            {
                _ = textures.Remove(channel);
                continue;
            }

            if (textures[channel] is JsonObject existing)
            {
                existing["virtual_path"] = virtualPath;
            }
            else
            {
                textures[channel] = new JsonObject { ["virtual_path"] = virtualPath };
            }
        }

        if (textures.Count == 0)
        {
            _ = descriptor.Remove("textures");
        }
        else
        {
            descriptor["textures"] = textures;
        }
    }
}
