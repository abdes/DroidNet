// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// One native import manifest job.
/// </summary>
public sealed record ContentImportJob(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("depends_on")] IReadOnlyList<string> DependsOn,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    [property: JsonPropertyName("output")] string? Output,
    [property: JsonPropertyName("name")] string? Name)
{
    /// <summary>Gets the ordered resolver-only roots for scene and geometry descriptors.</summary>
    [JsonPropertyName("cooked_context_roots")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyList<string>? CookedContextRoots { get; init; }

    /// <summary>Gets the required native source-content policy for model jobs.</summary>
    [JsonPropertyName("content_policy")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ContentPolicy { get; init; }

    /// <summary>Gets whether source transforms are baked into vertices.</summary>
    [JsonPropertyName("bake_transforms")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? BakeTransforms { get; init; }

    /// <summary>Gets the native coordinate-unit policy.</summary>
    [JsonPropertyName("unit_policy")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? UnitPolicy { get; init; }

    /// <summary>Gets the native normal policy.</summary>
    [JsonPropertyName("normals_policy")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? NormalsPolicy { get; init; }

    /// <summary>Gets the native tangent policy.</summary>
    [JsonPropertyName("tangents_policy")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? TangentsPolicy { get; init; }
}
