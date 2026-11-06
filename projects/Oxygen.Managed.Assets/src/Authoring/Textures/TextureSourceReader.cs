// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>Reads the engine's texture descriptor, <c>*.otex.json</c>.</summary>
public static class TextureSourceReader
{
    private static readonly Lazy<JsonSchema> Schema = new(static () => DescriptorFormat.LoadSchema("oxygen.texture-descriptor.schema.json"));

    /// <summary>Reads a texture descriptor, keeping the fields the model does not name.</summary>
    /// <param name="jsonUtf8">The descriptor bytes.</param>
    /// <param name="identity">The texture's identity, which its references are relative to.</param>
    /// <returns>The texture's source.</returns>
    /// <exception cref="AssetFormatException">
    /// The content is not a texture: its problem has one of the <see cref="TextureSourceCodes"/>, or the <c>vpath.*</c>
    /// code of a reference that does not resolve.
    /// </exception>
    public static TextureSource Read(ReadOnlySpan<byte> jsonUtf8, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        using var document = DescriptorFormat.Parse(jsonUtf8, TextureSourceCodes.NotJson, "The texture is not valid JSON.");
        var descriptor = document.RootElement;
        Validate(descriptor, identity);
        return new TextureSource(descriptor.GetProperty("source").GetString()!)
        {
            Name = Text(descriptor, "name"),
            Sources = descriptor.TryGetProperty("sources", out var sources) ? [.. sources.EnumerateArray().Select(ReadMapping)] : [],
            Intent = Text(descriptor, "intent"),
            ColorSpace = descriptor.TryGetProperty("decode", out var decode) ? Text(decode, "color_space") : null,
            Format = descriptor.TryGetProperty("output", out var output) ? Text(output, "format") : null,
            Cube = descriptor.TryGetProperty("cube", out var cube) ? ReadCube(cube) : null,
            Descriptor = descriptor.Clone(),
        };
    }

    /// <summary>Checks a descriptor against the engine schema and that each file reference resolves.</summary>
    /// <param name="descriptor">The descriptor.</param>
    /// <param name="identity">The texture's identity.</param>
    /// <exception cref="AssetFormatException">The descriptor has a problem.</exception>
    internal static void Validate(JsonElement descriptor, AssetPath identity)
    {
        var evaluation = Schema.Value.Evaluate(descriptor, new EvaluationOptions { OutputFormat = OutputFormat.List });
        if (!evaluation.IsValid)
        {
            throw DescriptorFormat.Problem(
                TextureSourceCodes.SchemaMismatch,
                "The texture does not match the engine's texture descriptor schema.",
                DescriptorFormat.Describe(evaluation));
        }

        ValidateReference(descriptor.GetProperty("source").GetString()!, identity);
        if (descriptor.TryGetProperty("sources", out var sources))
        {
            foreach (var mapping in sources.EnumerateArray())
            {
                ValidateReference(mapping.GetProperty("file").GetString()!, identity);
            }
        }
    }

    private static void ValidateReference(string reference, AssetPath identity)
    {
        if (!AssetPath.TryResolveReference(identity, reference, VirtualPathRuleSet.Cooked, out _, out var error))
        {
            throw DescriptorFormat.Problem(error.Value.ToCode(), $"The texture's image '{reference}' cannot be resolved: {error.Value.ToMessage()}");
        }
    }

    private static TextureSourceMapping ReadMapping(JsonElement mapping)
        => new(mapping.GetProperty("file").GetString()!)
        {
            Layer = Integer(mapping, "layer"),
            Mip = Integer(mapping, "mip"),
            Slice = Integer(mapping, "slice"),
        };

    private static TextureCubeSettings ReadCube(JsonElement cube)
        => new()
        {
            Cubemap = cube.TryGetProperty("cubemap", out var cubemap) ? cubemap.GetBoolean() : null,
            EquirectToCube = cube.TryGetProperty("equirect_to_cube", out var equirect) ? equirect.GetBoolean() : null,
            FaceSize = Integer(cube, "cube_face_size"),
            Layout = Text(cube, "cube_layout"),
        };

    private static string? Text(JsonElement parent, string name)
        => parent.TryGetProperty(name, out var value) ? value.GetString() : null;

    private static int? Integer(JsonElement parent, string name)
        => parent.TryGetProperty(name, out var value) ? value.GetInt32() : null;
}
