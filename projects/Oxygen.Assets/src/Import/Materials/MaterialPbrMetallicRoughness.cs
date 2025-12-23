// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// PBR metallic-roughness parameters aligned with the glTF 2.0 model.
/// </summary>
public sealed record MaterialPbrMetallicRoughness
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialPbrMetallicRoughness"/> class.
    /// </summary>
    /// <param name="baseColorR">Base color red channel.</param>
    /// <param name="baseColorG">Base color green channel.</param>
    /// <param name="baseColorB">Base color blue channel.</param>
    /// <param name="baseColorA">Base color alpha channel.</param>
    /// <param name="metallicFactor">Metallic factor.</param>
    /// <param name="roughnessFactor">Roughness factor.</param>
    /// <param name="baseColorTexture">Optional base color texture reference.</param>
    /// <param name="metallicRoughnessTexture">Optional packed metallic-roughness texture reference.</param>
    public MaterialPbrMetallicRoughness(
        float baseColorR,
        float baseColorG,
        float baseColorB,
        float baseColorA,
        float metallicFactor,
        float roughnessFactor,
        MaterialTextureRef? baseColorTexture,
        MaterialTextureRef? metallicRoughnessTexture)
    {
        this.BaseColorR = baseColorR;
        this.BaseColorG = baseColorG;
        this.BaseColorB = baseColorB;
        this.BaseColorA = baseColorA;
        this.MetallicFactor = metallicFactor;
        this.RoughnessFactor = roughnessFactor;
        this.BaseColorTexture = baseColorTexture;
        this.MetallicRoughnessTexture = metallicRoughnessTexture;
    }

    /// <summary>
    /// Gets the base color red channel.
    /// </summary>
    public float BaseColorR { get; }

    /// <summary>
    /// Gets the base color green channel.
    /// </summary>
    public float BaseColorG { get; }

    /// <summary>
    /// Gets the base color blue channel.
    /// </summary>
    public float BaseColorB { get; }

    /// <summary>
    /// Gets the base color alpha channel.
    /// </summary>
    public float BaseColorA { get; }

    /// <summary>
    /// Gets the metallic factor.
    /// </summary>
    public float MetallicFactor { get; }

    /// <summary>
    /// Gets the roughness factor.
    /// </summary>
    public float RoughnessFactor { get; }

    /// <summary>
    /// Gets the optional base color texture reference.
    /// </summary>
    public MaterialTextureRef? BaseColorTexture { get; }

    /// <summary>
    /// Gets the optional packed metallic-roughness texture reference.
    /// </summary>
    public MaterialTextureRef? MetallicRoughnessTexture { get; }
}
