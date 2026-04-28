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
    [property: JsonPropertyName("name")] string? Name);
