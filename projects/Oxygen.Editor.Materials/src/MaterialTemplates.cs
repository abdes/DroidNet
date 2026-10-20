// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.Materials;

/// <summary>The source of a new material, and the template New Material creates it from.</summary>
public static class MaterialTemplates
{
    /// <summary>Gets the template of New Material: a material source in <c>Materials</c>, named after its file.</summary>
    public static AuthoredAssetTemplate Asset { get; } = new()
    {
        Folder = "Materials",
        DefaultName = "NewMaterial",
        Suffixes = [AssetFileTypes.MaterialDescriptor],
        Content = New,
    };

    /// <summary>Makes a new material's source: white, opaque and single-sided, named after its file.</summary>
    /// <param name="materialName">The material's name, which is its file's name.</param>
    /// <returns>The material's descriptor JSON, as the material writer writes it.</returns>
    public static string New(string materialName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(materialName);
        var source = new MaterialSource(
            name: materialName,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 1.0f,
                baseColorG: 1.0f,
                baseColorB: 1.0f,
                baseColorA: 1.0f,
                metallicFactor: 0.0f,
                roughnessFactor: 0.5f),
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);
        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source);
        return Encoding.UTF8.GetString(memory.ToArray());
    }
}
