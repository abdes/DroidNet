// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Numerics;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>Reads the canonical engine material descriptor without legacy conversion.</summary>
public static class MaterialSourceReader
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);

    /// <summary>Reads schema-valid source while retaining all unedited native fields.</summary>
    /// <param name="jsonUtf8">Canonical UTF-8 descriptor bytes.</param>
    /// <returns>The immutable authoring snapshot.</returns>
    /// <exception cref="AssetFormatException">
    /// The content is not a material: its problem has one of the <see cref="MaterialSourceCodes"/>.
    /// </exception>
    public static MaterialSource Read(ReadOnlySpan<byte> jsonUtf8)
    {
        using var document = Parse(jsonUtf8);
        var descriptor = document.RootElement;
        Validate(descriptor);
        var parameters = descriptor.TryGetProperty("parameters", out var values) ? values : default;
        var textures = descriptor.TryGetProperty("textures", out var bindings) ? bindings : default;
        var baseColor = Vector(parameters, "base_color", [1.0f, 1.0f, 1.0f, 1.0f]);
        var emission = Vector(parameters, "emissive_color", [1.0f, 1.0f, 1.0f]);
        return new MaterialSource(
            name: descriptor.TryGetProperty("name", out var name) ? name.GetString() : null,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColor[0], baseColor[1], baseColor[2], baseColor[3],
                Scalar(parameters, "metalness", 0.0f), MaterialSource.ResolveRoughness(parameters)),
            alphaMode: MaterialSource.ResolveAlphaMode(descriptor),
            alphaCutoff: Scalar(parameters, "alpha_cutoff", 0.5f),
            doubleSided: parameters.ValueKind == JsonValueKind.Object
                && parameters.TryGetProperty("double_sided", out var sided) && sided.GetBoolean(),
            emissiveColor: new Vector3(emission[0], emission[1], emission[2]),
            emissiveIntensity: Scalar(parameters, "emissive_intensity", 0.0f))
        {
            Descriptor = descriptor.Clone(),
            NormalScale = Scalar(parameters, "normal_scale", 1.0f),
            OcclusionStrength = Scalar(parameters, "ambient_occlusion", 1.0f),
            TextureReferences = ReadTextureReferences(textures),
        };
    }

    /// <summary>Checks a descriptor against the engine schema and the engine's storage of numbers and paths.</summary>
    /// <param name="descriptor">The descriptor.</param>
    /// <exception cref="AssetFormatException">The descriptor has a problem.</exception>
    internal static void Validate(JsonElement descriptor)
    {
        var evaluation = Schema.Value.Evaluate(descriptor, new EvaluationOptions { OutputFormat = OutputFormat.List });
        if (!evaluation.IsValid)
        {
            throw Problem(
                MaterialSourceCodes.SchemaMismatch,
                "The material does not match the engine's material descriptor schema.",
                string.Join(Environment.NewLine, SchemaErrors(evaluation)));
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
                if (!MaterialSource.IsTexturePath(path))
                {
                    throw Problem(MaterialSourceCodes.TexturePathInvalid, $"The {texture.Name} texture '{path}' is not a canonical absolute virtual path below a mount point.");
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
                throw Problem(MaterialSourceCodes.NumberOutOfRange, "A material number does not fit a finite 32-bit float.");
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

    private static JsonDocument Parse(ReadOnlySpan<byte> jsonUtf8)
    {
        try
        {
            return JsonDocument.Parse(jsonUtf8.ToArray(), new JsonDocumentOptions
            {
                AllowTrailingCommas = true,
                CommentHandling = JsonCommentHandling.Skip,
            });
        }
        catch (JsonException exception)
        {
            var problem = new AssetProblem(DiagnosticSeverity.Error, MaterialSourceCodes.NotJson, "The material is not valid JSON.")
            {
                Line = (int?)(exception.LineNumber + 1),
                Column = (int?)(exception.BytePositionInLine + 1),
                Explanation = exception.Message,
            };
            throw new AssetFormatException(problem, exception);
        }
    }

    private static AssetFormatException Problem(string code, string message, string? explanation = null)
        => new(new AssetProblem(DiagnosticSeverity.Error, code, message) { Explanation = explanation });

    private static IEnumerable<string> SchemaErrors(EvaluationResults evaluation)
    {
        foreach (var (keyword, error) in evaluation.Errors ?? new Dictionary<string, string>(StringComparer.Ordinal))
        {
            yield return $"{evaluation.InstanceLocation}: {keyword} {error}";
        }

        foreach (var detail in evaluation.Details ?? [])
        {
            foreach (var error in SchemaErrors(detail))
            {
                yield return error;
            }
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
            : throw Problem(MaterialSourceCodes.NumberOutOfRange, $"The material parameter '{name}' does not fit a finite 32-bit float.");
    }

    private static float[] Vector(JsonElement parent, string name, float[] fallback)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(name, out var property))
        {
            return fallback;
        }

        var result = property.EnumerateArray().Select(static item => item.GetSingle()).ToArray();
        return result.All(float.IsFinite) ? result
            : throw Problem(MaterialSourceCodes.NumberOutOfRange, $"The material parameter '{name}' does not fit a finite 32-bit float.");
    }

    private static IReadOnlyDictionary<string, string> ReadTextureReferences(JsonElement textures)
    {
        var references = new Dictionary<string, string>(StringComparer.Ordinal);
        if (textures.ValueKind == JsonValueKind.Object)
        {
            foreach (var channel in MaterialSource.TextureChannels)
            {
                if (textures.TryGetProperty(channel, out var binding))
                {
                    references.Add(channel, binding.GetProperty("virtual_path").GetString()!);
                }
            }
        }

        return references.ToFrozenDictionary(StringComparer.Ordinal);
    }
}
