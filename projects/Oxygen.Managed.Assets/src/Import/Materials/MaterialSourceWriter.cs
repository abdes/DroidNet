// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;

namespace Oxygen.Managed.Assets.Import.Materials;

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
        ValidateReadOnlyTextureReferences(material);
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
        if (material.NormalTexture is { } normal)
        {
            parameters["normal_scale"] = normal.Scale;
        }

        if (material.OcclusionTexture is { } occlusion)
        {
            parameters["ambient_occlusion"] = occlusion.Strength;
        }

        // Existing canonical bindings include independent channels and UV transforms.
        // These are read-only in the scalar editor and retain their exact representation.
        if (material.Descriptor is null)
        {
            var textures = new JsonObject();
            AddTexture(textures, "base_color", pbr.BaseColorTexture?.Source);
            AddTexture(textures, "normal", material.NormalTexture?.Source);
            AddTexture(textures, "ambient_occlusion", material.OcclusionTexture?.Source);
            AddTexture(textures, "metallic", pbr.MetallicRoughnessTexture?.Source);
            AddTexture(textures, "roughness", pbr.MetallicRoughnessTexture?.Source);
            if (textures.Count > 0)
            {
                descriptor["textures"] = textures;
            }
        }

        MaterialSourceReader.Validate(JsonSerializer.SerializeToElement(descriptor));
        return descriptor;
    }

    private static void AddTexture(JsonObject textures, string name, string? source)
    {
        if (source is not null)
        {
            textures[name] = new JsonObject { ["virtual_path"] = source };
        }
    }

    private static void ValidateReadOnlyTextureReferences(MaterialSource material)
    {
        if (material.Descriptor is not { } descriptor)
        {
            return;
        }

        var textures = descriptor.TryGetProperty("textures", out var bindings) ? bindings : default;
        string? ReadPath(string name) => textures.ValueKind == JsonValueKind.Object && textures.TryGetProperty(name, out var binding)
            ? binding.GetProperty("virtual_path").GetString() : null;
        if (!string.Equals(material.PbrMetallicRoughness.BaseColorTexture?.Source, ReadPath("base_color"), StringComparison.Ordinal)
            || !string.Equals(material.PbrMetallicRoughness.MetallicRoughnessTexture?.Source, MaterialSource.PackedMetallicRoughnessPath(textures), StringComparison.Ordinal)
            || !string.Equals(material.NormalTexture?.Source, ReadPath("normal"), StringComparison.Ordinal)
            || !string.Equals(material.OcclusionTexture?.Source, ReadPath("ambient_occlusion"), StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Texture identities are read-only in the scalar material editor.");
        }
    }
}
