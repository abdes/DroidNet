// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>One pipeline's execution settings in a native import manifest's <c>concurrency</c>.</summary>
/// <param name="Workers">The pipeline's workers; null for the ImportTool's default.</param>
/// <param name="QueueCapacity">The capacity of its work queues; null for the ImportTool's default.</param>
public sealed record ContentImportConcurrency(
    [property: JsonPropertyName("workers")]
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? Workers,
    [property: JsonPropertyName("queue_capacity")]
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] int? QueueCapacity);
