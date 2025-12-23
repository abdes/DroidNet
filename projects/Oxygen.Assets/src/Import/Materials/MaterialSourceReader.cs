// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Reads material sources from the authoring JSON format (<c>*.omat.json</c>).
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
public static class MaterialSourceReader
{
    private const string ExpectedSchema = "oxygen.material.v1";
    private const string ExpectedType = "PBR";

    /// <summary>
    /// Reads a <see cref="MaterialSource"/> from JSON text.
    /// </summary>
    /// <param name="jsonUtf8">The JSON payload as UTF-8 bytes.</param>
    /// <returns>The parsed material source.</returns>
    public static MaterialSource Read(ReadOnlySpan<byte> jsonUtf8)
    {
        var input = JsonSerializer.Deserialize(jsonUtf8, Serialization.Context.MaterialSourceData)
            ?? throw new InvalidDataException("Material JSON is empty or invalid.");

        if (!string.Equals(input.Schema, ExpectedSchema, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Unsupported material schema '{input.Schema}'.");
        }

        if (string.IsNullOrWhiteSpace(input.Type))
        {
            throw new InvalidDataException("Material JSON is missing required field 'Type'.");
        }

        if (!string.Equals(input.Type, ExpectedType, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Unsupported material type '{input.Type}'.");
        }

        var alphaMode = ParseAlphaMode(input.AlphaMode);
        var alphaCutoff = Clamp01(input.AlphaCutoff ?? 0.5f);

        var pbrModel = ParsePbrMetallicRoughness(input.PbrMetallicRoughness);
        var normalTexture = ParseNormalTexture(input.NormalTexture);
        var occlusionTexture = ParseOcclusionTexture(input.OcclusionTexture);

        return new MaterialSource(
            schema: input.Schema,
            type: input.Type,
            name: input.Name,
            pbrMetallicRoughness: pbrModel,
            normalTexture: normalTexture,
            occlusionTexture: occlusionTexture,
            alphaMode: alphaMode,
            alphaCutoff: alphaCutoff,
            doubleSided: input.DoubleSided ?? false);
    }

    private static MaterialPbrMetallicRoughness ParsePbrMetallicRoughness(PbrMetallicRoughnessData? input)
    {
        var pbr = input ?? new PbrMetallicRoughnessData();
        var baseColor = pbr.BaseColorFactor ?? [1.0f, 1.0f, 1.0f, 1.0f];
        if (baseColor.Length != 4)
        {
            throw new InvalidDataException("PbrMetallicRoughness.BaseColorFactor must have exactly 4 components.");
        }

        return new MaterialPbrMetallicRoughness(
            baseColorR: Clamp01(baseColor[0]),
            baseColorG: Clamp01(baseColor[1]),
            baseColorB: Clamp01(baseColor[2]),
            baseColorA: Clamp01(baseColor[3]),
            metallicFactor: Clamp01(pbr.MetallicFactor ?? 1.0f),
            roughnessFactor: Clamp01(pbr.RoughnessFactor ?? 1.0f),
            baseColorTexture: ParseTextureRef(pbr.BaseColorTexture),
            metallicRoughnessTexture: ParseTextureRef(pbr.MetallicRoughnessTexture));
    }

    private static MaterialTextureRef? ParseTextureRef(TextureRefData? input)
    {
        if (input is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(input.Source))
        {
            throw new InvalidDataException("Texture reference is missing required field 'Source'.");
        }

        if (input.Scale is not null || input.Strength is not null)
        {
            throw new InvalidDataException("Texture reference contains unexpected properties (only 'Source' is allowed here).");
        }

        return new MaterialTextureRef(input.Source);
    }

    private static NormalTextureRef? ParseNormalTexture(TextureRefData? input)
    {
        if (input is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(input.Source))
        {
            throw new InvalidDataException("NormalTexture is missing required field 'Source'.");
        }

        if (input.Strength is not null)
        {
            throw new InvalidDataException("NormalTexture contains unexpected property 'Strength'.");
        }

        return new NormalTextureRef(
            Source: input.Source,
            Scale: Math.Max(0.0f, input.Scale ?? 1.0f));
    }

    private static OcclusionTextureRef? ParseOcclusionTexture(TextureRefData? input)
    {
        if (input is null)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(input.Source))
        {
            throw new InvalidDataException("OcclusionTexture is missing required field 'Source'.");
        }

        if (input.Scale is not null)
        {
            throw new InvalidDataException("OcclusionTexture contains unexpected property 'Scale'.");
        }

        return new OcclusionTextureRef(
            Source: input.Source,
            Strength: Clamp01(input.Strength ?? 1.0f));
    }

    private static MaterialAlphaMode ParseAlphaMode(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return MaterialAlphaMode.Opaque;
        }

        return value switch
        {
            "OPAQUE" => MaterialAlphaMode.Opaque,
            "MASK" => MaterialAlphaMode.Mask,
            "BLEND" => MaterialAlphaMode.Blend,
            _ => throw new InvalidDataException($"Unsupported AlphaMode '{value}'."),
        };
    }

    private static float Clamp01(float value)
        => Math.Clamp(value, 0.0f, 1.0f);
}
