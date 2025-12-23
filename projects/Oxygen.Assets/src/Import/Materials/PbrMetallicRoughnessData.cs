// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Data model for the <c>PbrMetallicRoughness</c> object in authoring material JSON.
/// </summary>
internal sealed class PbrMetallicRoughnessData
{
    /// <summary>
    /// Gets or sets the base color factor (RGBA, 4 components).
    /// </summary>
    [JsonPropertyName("BaseColorFactor")]
    public float[]? BaseColorFactor { get; set; }

    /// <summary>
    /// Gets or sets the metallic factor.
    /// </summary>
    [JsonPropertyName("MetallicFactor")]
    public float? MetallicFactor { get; set; }

    /// <summary>
    /// Gets or sets the roughness factor.
    /// </summary>
    [JsonPropertyName("RoughnessFactor")]
    public float? RoughnessFactor { get; set; }

    /// <summary>
    /// Gets or sets the optional base color texture reference.
    /// </summary>
    [JsonPropertyName("BaseColorTexture")]
    public TextureRefData? BaseColorTexture { get; set; }

    /// <summary>
    /// Gets or sets the optional packed metallic-roughness texture reference.
    /// </summary>
    [JsonPropertyName("MetallicRoughnessTexture")]
    public TextureRefData? MetallicRoughnessTexture { get; set; }
}
