// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Filesystem;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>Reads the canonical engine material descriptor without legacy conversion.</summary>
public static class MaterialSourceReader
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);

    /// <summary>Reads schema-valid source while retaining all unedited native fields.</summary>
    /// <param name="jsonUtf8">Canonical UTF-8 descriptor bytes.</param>
    /// <returns>The immutable authoring snapshot.</returns>
    public static MaterialSource Read(ReadOnlySpan<byte> jsonUtf8)
    {
        using var document = JsonDocument.Parse(jsonUtf8.ToArray(), new JsonDocumentOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip,
        });
        var descriptor = document.RootElement;
        Validate(descriptor);
        var parameters = descriptor.TryGetProperty("parameters", out var values) ? values : default;
        var textures = descriptor.TryGetProperty("textures", out var bindings) ? bindings : default;
        var baseColor = Vector(parameters, "base_color", [1.0f, 1.0f, 1.0f, 1.0f]);
        var emission = Vector(parameters, "emissive_color", [1.0f, 1.0f, 1.0f]);
        var normal = Texture(textures, "normal");
        var occlusion = Texture(textures, "ambient_occlusion");
        var metallicRoughness = MaterialSource.PackedMetallicRoughnessPath(textures);
        return new MaterialSource(
            name: descriptor.TryGetProperty("name", out var name) ? name.GetString() : null,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColor[0], baseColor[1], baseColor[2], baseColor[3],
                Scalar(parameters, "metalness", 0.0f), MaterialSource.ResolveRoughness(parameters),
                Texture(textures, "base_color"), metallicRoughness is null ? null : new MaterialTextureRef(metallicRoughness)),
            normalTexture: normal is { } normalRef
                ? new NormalTextureRef(normalRef.Source, Scalar(parameters, "normal_scale", 1.0f)) : null,
            occlusionTexture: occlusion is { } occlusionRef
                ? new OcclusionTextureRef(occlusionRef.Source, Scalar(parameters, "ambient_occlusion", 1.0f)) : null,
            alphaMode: MaterialSource.ResolveAlphaMode(descriptor),
            alphaCutoff: Scalar(parameters, "alpha_cutoff", 0.5f),
            doubleSided: parameters.ValueKind == JsonValueKind.Object
                && parameters.TryGetProperty("double_sided", out var sided) && sided.GetBoolean(),
            emissiveColor: new Vector3(emission[0], emission[1], emission[2]),
            emissiveIntensity: Scalar(parameters, "emissive_intensity", 0.0f))
        {
            Descriptor = descriptor.Clone(),
        };
    }

    internal static void Validate(JsonElement descriptor)
    {
        if (!Schema.Value.Evaluate(descriptor).IsValid)
        {
            throw new InvalidDataException("Material source does not match oxygen.material-descriptor.schema.json.");
        }

        if (descriptor.TryGetProperty("parameters", out var parameters))
        {
            ValidateFloat32(parameters);
        }

        if (descriptor.TryGetProperty("textures", out var textures))
        {
            foreach (var texture in textures.EnumerateObject())
            {
                var binding = texture.Value;
                var path = binding.GetProperty("virtual_path").GetString()!;
                if (!VirtualPath.IsCanonicalAbsolute(path) || path.IndexOf('/', 1) < 2)
                {
                    throw new InvalidDataException($"Texture '{texture.Name}' requires a canonical virtual asset path, received '{path}'.");
                }

                if (binding.TryGetProperty("uv_transform", out var transform))
                {
                    ValidateFloat32(transform);
                }
            }
        }
    }

    private static void ValidateFloat32(JsonElement value)
    {
        switch (value.ValueKind)
        {
            case JsonValueKind.Number when !value.TryGetSingle(out var number) || !float.IsFinite(number):
                throw new InvalidDataException("Material numeric values must fit finite float32 storage.");
            case JsonValueKind.Array:
                foreach (var element in value.EnumerateArray())
                {
                    ValidateFloat32(element);
                }

                break;
            case JsonValueKind.Object:
                foreach (var property in value.EnumerateObject())
                {
                    ValidateFloat32(property.Value);
                }

                break;
        }
    }

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(MaterialSourceReader).Assembly.GetManifestResourceStream(
            "oxygen.material-descriptor.schema.json")
            ?? throw new InvalidOperationException("The canonical material schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }

    private static float Scalar(JsonElement parent, string name, float fallback)
    {
        var value = parent.ValueKind == JsonValueKind.Object && parent.TryGetProperty(name, out var property)
            ? property.GetSingle() : fallback;
        return float.IsFinite(value) ? value
            : throw new InvalidDataException($"Material parameter '{name}' exceeds finite float32 range.");
    }

    private static float[] Vector(JsonElement parent, string name, float[] fallback)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(name, out var property))
        {
            return fallback;
        }

        var result = property.EnumerateArray().Select(static item => item.GetSingle()).ToArray();
        return result.All(float.IsFinite) ? result
            : throw new InvalidDataException($"Material parameter '{name}' exceeds finite float32 range.");
    }

    private static MaterialTextureRef? Texture(JsonElement textures, string name)
        => textures.ValueKind == JsonValueKind.Object && textures.TryGetProperty(name, out var binding)
            ? new MaterialTextureRef(binding.GetProperty("virtual_path").GetString()!) : null;
}
