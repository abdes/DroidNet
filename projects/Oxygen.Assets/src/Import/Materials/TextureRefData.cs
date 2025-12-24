// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Assets.Import.Materials;

/// <summary>
/// Data model for a texture reference in authoring material JSON.
/// </summary>
internal sealed class TextureRefData
{
    /// <summary>
    /// Gets or sets an <c>asset:///</c> URI referencing an authoring texture source.
    /// </summary>
    [JsonPropertyName("Source")]
    public string Source { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets an optional scale factor (used by <c>NormalTexture</c>).
    /// </summary>
    [JsonPropertyName("Scale")]
    public float? Scale { get; set; }

    /// <summary>
    /// Gets or sets an optional strength factor (used by <c>OcclusionTexture</c>).
    /// </summary>
    [JsonPropertyName("Strength")]
    public float? Strength { get; set; }
}
