// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Authoring-time material definition parsed from <c>*.omat.json</c>.
/// </summary>
/// <remarks>
/// This model is used by import/build tooling. Runtime consumes the cooked binary <c>.omat</c> format.
/// </remarks>
public sealed record MaterialSource
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialSource"/> class.
    /// </summary>
    /// <param name="schema">The schema identifier (e.g. <c>oxygen.material.v1</c>).</param>
    /// <param name="type">The material type/model (MVP: <c>PBR</c>).</param>
    /// <param name="name">An optional debugging/display name.</param>
    /// <param name="pbrMetallicRoughness">The PBR metallic-roughness parameters.</param>
    /// <param name="normalTexture">The optional normal texture reference and scale.</param>
    /// <param name="occlusionTexture">The optional occlusion texture reference and strength.</param>
    /// <param name="alphaMode">The alpha mode semantics.</param>
    /// <param name="alphaCutoff">The alpha cutoff used for <see cref="MaterialAlphaMode.Mask"/>.</param>
    /// <param name="doubleSided">Whether the material should be treated as double-sided.</param>
    public MaterialSource(
        string schema,
        string type,
        string? name,
        MaterialPbrMetallicRoughness pbrMetallicRoughness,
        NormalTextureRef? normalTexture,
        OcclusionTextureRef? occlusionTexture,
        MaterialAlphaMode alphaMode,
        float alphaCutoff,
        bool doubleSided)
    {
        this.Schema = schema;
        this.Type = type;
        this.Name = name;
        this.PbrMetallicRoughness = pbrMetallicRoughness;
        this.NormalTexture = normalTexture;
        this.OcclusionTexture = occlusionTexture;
        this.AlphaMode = alphaMode;
        this.AlphaCutoff = alphaCutoff;
        this.DoubleSided = doubleSided;
    }

    /// <summary>
    /// Gets the schema identifier.
    /// </summary>
    public string Schema { get; }

    /// <summary>
    /// Gets the material type/model.
    /// </summary>
    public string Type { get; }

    /// <summary>
    /// Gets the optional debugging/display name.
    /// </summary>
    public string? Name { get; }

    /// <summary>
    /// Gets the PBR metallic-roughness parameters.
    /// </summary>
    public MaterialPbrMetallicRoughness PbrMetallicRoughness { get; }

    /// <summary>
    /// Gets the optional normal texture reference and scale.
    /// </summary>
    public NormalTextureRef? NormalTexture { get; }

    /// <summary>
    /// Gets the optional occlusion texture reference and strength.
    /// </summary>
    public OcclusionTextureRef? OcclusionTexture { get; }

    /// <summary>
    /// Gets the alpha mode.
    /// </summary>
    public MaterialAlphaMode AlphaMode { get; }

    /// <summary>
    /// Gets the alpha cutoff used for masked materials.
    /// </summary>
    public float AlphaCutoff { get; }

    /// <summary>
    /// Gets a value indicating whether the material should be treated as double-sided.
    /// </summary>
    public bool DoubleSided { get; }
}
