// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Scalar material parameters in the native Oxygen.Cooker material-descriptor schema.
/// </summary>
internal sealed record NativeMaterialParameters(
    [property: JsonPropertyName("base_color")] float[] BaseColor,
    [property: JsonPropertyName("metalness")] float Metalness,
    [property: JsonPropertyName("roughness")] float Roughness,
    [property: JsonPropertyName("double_sided")] bool DoubleSided,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    [property: JsonPropertyName("alpha_cutoff")] float? AlphaCutoff);
