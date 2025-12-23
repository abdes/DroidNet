// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Writes material sources to the authoring JSON format (<c>*.omat.json</c>).
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
public static class MaterialSourceWriter
{
    /// <summary>
    /// Writes the given material source to JSON.
    /// </summary>
    /// <param name="output">The output stream.</param>
    /// <param name="material">The material source to serialize.</param>
    public static void Write(Stream output, MaterialSource material)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(material);

        var data = ToData(material);
        JsonSerializer.Serialize(output, data, Serialization.Context.MaterialSourceData);
    }

    private static MaterialSourceData ToData(MaterialSource material)
        => new()
        {
            Schema = material.Schema,
            Type = material.Type,
            Name = material.Name,
            PbrMetallicRoughness = ToPbrData(material.PbrMetallicRoughness),
            NormalTexture = ToNormalTextureDto(material.NormalTexture),
            OcclusionTexture = ToOcclusionTextureDto(material.OcclusionTexture),
            AlphaMode = ToAlphaMode(material.AlphaMode),
            AlphaCutoff = material.AlphaMode == MaterialAlphaMode.Mask ? material.AlphaCutoff : null,
            DoubleSided = material.DoubleSided,
        };

    private static PbrMetallicRoughnessData ToPbrData(MaterialPbrMetallicRoughness pbr)
        => new()
        {
            BaseColorFactor = [pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA],
            MetallicFactor = pbr.MetallicFactor,
            RoughnessFactor = pbr.RoughnessFactor,
            BaseColorTexture = ToTextureRefDto(pbr.BaseColorTexture),
            MetallicRoughnessTexture = ToTextureRefDto(pbr.MetallicRoughnessTexture),
        };

    private static TextureRefData? ToTextureRefDto(MaterialTextureRef? texture)
    {
        if (texture is not { } value)
        {
            return null;
        }

        return new TextureRefData { Source = value.Source };
    }

    private static TextureRefData? ToNormalTextureDto(NormalTextureRef? texture)
    {
        if (texture is not { } value)
        {
            return null;
        }

        return new TextureRefData { Source = value.Source, Scale = value.Scale };
    }

    private static TextureRefData? ToOcclusionTextureDto(OcclusionTextureRef? texture)
    {
        if (texture is not { } value)
        {
            return null;
        }

        return new TextureRefData { Source = value.Source, Strength = value.Strength };
    }

    private static string ToAlphaMode(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Opaque => "OPAQUE",
            MaterialAlphaMode.Mask => "MASK",
            MaterialAlphaMode.Blend => "BLEND",
            _ => "OPAQUE",
        };
}
