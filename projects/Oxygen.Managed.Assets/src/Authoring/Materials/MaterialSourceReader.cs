// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Numerics;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>Reads the canonical engine material descriptor without legacy conversion.</summary>
public static class MaterialSourceReader
{
    private static readonly Lazy<JsonSchema> Schema = new(static () => DescriptorFormat.LoadSchema("oxygen.material-descriptor.schema.json"));

    /// <summary>Reads schema-valid source while retaining all unedited native fields.</summary>
    /// <param name="jsonUtf8">Canonical UTF-8 descriptor bytes.</param>
    /// <param name="identity">The material's identity, which its references are relative to.</param>
    /// <returns>The immutable authoring snapshot.</returns>
    /// <exception cref="AssetFormatException">
    /// The content is not a material: its problem has one of the <see cref="MaterialSourceCodes"/>, or the <c>vpath.*</c>
    /// code of a reference that does not resolve.
    /// </exception>
    public static MaterialSource Read(ReadOnlySpan<byte> jsonUtf8, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        using var document = DescriptorFormat.Parse(jsonUtf8, MaterialSourceCodes.NotJson, "The material is not valid JSON.");
        var descriptor = document.RootElement;
        Validate(descriptor, identity);
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
            AmbientOcclusionMode = descriptor.TryGetProperty("ambient_occlusion_mode", out var occlusion) && occlusion.GetString() == "strength"
                ? MaterialAmbientOcclusionMode.Strength
                : MaterialAmbientOcclusionMode.Factor,
            ProceduralGrid = descriptor.TryGetProperty("procedural_grid", out var grid) ? ReadGrid(grid) : null,
            TextureBindings = ReadTextureBindings(textures, identity),
        };
    }

    /// <summary>Checks a descriptor against the engine schema, whose bounds keep each number in a finite 32-bit float, and that each reference resolves.</summary>
    /// <param name="descriptor">The descriptor.</param>
    /// <param name="identity">The material's identity.</param>
    /// <exception cref="AssetFormatException">The descriptor has a problem.</exception>
    internal static void Validate(JsonElement descriptor, AssetPath identity)
    {
        var evaluation = Schema.Value.Evaluate(descriptor, new EvaluationOptions { OutputFormat = OutputFormat.List });
        if (!evaluation.IsValid)
        {
            throw DescriptorFormat.Problem(
                MaterialSourceCodes.SchemaMismatch,
                "The material does not match the engine's material descriptor schema.",
                DescriptorFormat.Describe(evaluation));
        }

        if (descriptor.TryGetProperty("textures", out var textures))
        {
            foreach (var texture in textures.EnumerateObject())
            {
                var reference = texture.Value.GetProperty("virtual_path").GetString()!;
                if (!AssetPath.TryResolveReference(identity, reference, VirtualPathRuleSet.Authored, out _, out var error))
                {
                    throw DescriptorFormat.Problem(error.Value.ToCode(), $"The {texture.Name} texture '{reference}' cannot be resolved: {error.Value.ToMessage()}");
                }
            }
        }
    }

    private static float Scalar(JsonElement parent, string name, float fallback)
        => parent.ValueKind == JsonValueKind.Object && parent.TryGetProperty(name, out var property) ? property.GetSingle() : fallback;

    private static float[] Vector(JsonElement parent, string name, float[] fallback)
        => parent.ValueKind == JsonValueKind.Object && parent.TryGetProperty(name, out var property)
            ? [.. property.EnumerateArray().Select(static item => item.GetSingle())]
            : fallback;

    private static FrozenDictionary<string, MaterialTextureBinding> ReadTextureBindings(JsonElement textures, AssetPath identity)
    {
        var bindings = new Dictionary<string, MaterialTextureBinding>(StringComparer.Ordinal);
        if (textures.ValueKind == JsonValueKind.Object)
        {
            foreach (var channel in MaterialSource.TextureChannels)
            {
                if (textures.TryGetProperty(channel, out var binding))
                {
                    bindings.Add(channel, ReadBinding(binding, identity));
                }
            }
        }

        return bindings.ToFrozenDictionary(StringComparer.Ordinal);
    }

    // Validate has resolved the reference and checked the rest against the schema.
    private static MaterialTextureBinding ReadBinding(JsonElement binding, AssetPath identity)
    {
        _ = AssetPath.TryResolveReference(identity, binding.GetProperty("virtual_path").GetString()!, VirtualPathRuleSet.Authored, out var texture, out _);
        return new MaterialTextureBinding(texture!)
        {
            UvSet = binding.TryGetProperty("uv_set", out var set) ? set.GetInt32() : 0,
            UvTransform = binding.TryGetProperty("uv_transform", out var transform) ? ReadTransform(transform) : MaterialUvTransform.Identity,
            Sampler = binding.TryGetProperty("sampler", out var sampler) ? ReadSampler(sampler) : MaterialTextureSampler.Default,
        };
    }

    private static MaterialUvTransform ReadTransform(JsonElement transform)
        => new(
            Pair(transform, "scale") ?? Vector2.One,
            Pair(transform, "offset") ?? Vector2.Zero,
            OptionalScalar(transform, "rotation_radians") ?? 0.0f);

    private static MaterialTextureSampler ReadSampler(JsonElement sampler)
        => new()
        {
            AddressU = Parse(sampler, "address_u", MaterialTextureAddress.Repeat),
            AddressV = Parse(sampler, "address_v", MaterialTextureAddress.Repeat),
            MagFilter = Parse(sampler, "mag_filter", MaterialTextureFilter.Linear),
            MinFilter = Parse(sampler, "min_filter", MaterialTextureFilter.Linear),
            MipFilter = Parse(sampler, "mip_filter", MaterialTextureMipFilter.Linear),
        };

    private static MaterialProceduralGrid ReadGrid(JsonElement grid)
        => new()
        {
            Spacing = Pair(grid, "spacing"),
            MajorEvery = grid.TryGetProperty("major_every", out var every) ? every.GetUInt32() : null,
            LineThickness = OptionalScalar(grid, "line_thickness"),
            MajorThickness = OptionalScalar(grid, "major_thickness"),
            AxisThickness = OptionalScalar(grid, "axis_thickness"),
            FadeStart = OptionalScalar(grid, "fade_start"),
            FadeEnd = OptionalScalar(grid, "fade_end"),
            MinorColor = Color(grid, "minor_color"),
            MajorColor = Color(grid, "major_color"),
            AxisColorX = Color(grid, "axis_color_x"),
            AxisColorY = Color(grid, "axis_color_y"),
            OriginColor = Color(grid, "origin_color"),
        };

    private static float? OptionalScalar(JsonElement parent, string name)
        => parent.TryGetProperty(name, out var value) ? value.GetSingle() : null;

    private static Vector2? Pair(JsonElement parent, string name)
        => parent.TryGetProperty(name, out var value) ? new Vector2(value[0].GetSingle(), value[1].GetSingle()) : null;

    private static Vector4? Color(JsonElement parent, string name)
        => parent.TryGetProperty(name, out var value) ? new Vector4(value[0].GetSingle(), value[1].GetSingle(), value[2].GetSingle(), value[3].GetSingle()) : null;

    private static T Parse<T>(JsonElement parent, string name, T fallback)
        where T : struct, Enum
        => parent.TryGetProperty(name, out var value) ? Enum.Parse<T>(value.GetString()!, ignoreCase: true) : fallback;
}
