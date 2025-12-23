// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Data model for the material authoring JSON schema (<c>*.omat.json</c>).
/// </summary>
internal sealed class MaterialSourceData
{
    /// <summary>
    /// Gets or sets the schema identifier (e.g. <c>oxygen.material.v1</c>).
    /// </summary>
    [JsonPropertyName("Schema")]
    public string Schema { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the material type/model (MVP: <c>PBR</c>).
    /// </summary>
    [JsonPropertyName("Type")]
    public string Type { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the optional debugging/display name.
    /// </summary>
    [JsonPropertyName("Name")]
    public string? Name { get; set; }

    /// <summary>
    /// Gets or sets the PBR metallic-roughness parameters.
    /// </summary>
    [JsonPropertyName("PbrMetallicRoughness")]
    public PbrMetallicRoughnessData? PbrMetallicRoughness { get; set; }

    /// <summary>
    /// Gets or sets the optional normal texture reference.
    /// </summary>
    [JsonPropertyName("NormalTexture")]
    public TextureRefData? NormalTexture { get; set; }

    /// <summary>
    /// Gets or sets the optional occlusion texture reference.
    /// </summary>
    [JsonPropertyName("OcclusionTexture")]
    public TextureRefData? OcclusionTexture { get; set; }

    /// <summary>
    /// Gets or sets the alpha mode (<c>OPAQUE</c>, <c>MASK</c>, <c>BLEND</c>).
    /// </summary>
    [JsonPropertyName("AlphaMode")]
    public string? AlphaMode { get; set; }

    /// <summary>
    /// Gets or sets the alpha cutoff used for <c>MASK</c> materials.
    /// </summary>
    [JsonPropertyName("AlphaCutoff")]
    public float? AlphaCutoff { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether the material should be treated as double-sided.
    /// </summary>
    [JsonPropertyName("DoubleSided")]
    public bool? DoubleSided { get; set; }
}
