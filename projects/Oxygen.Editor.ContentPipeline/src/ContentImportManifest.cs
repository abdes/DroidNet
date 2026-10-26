// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Editor representation of an Oxygen import manifest.
/// </summary>
/// <param name="Version">The native manifest version.</param>
/// <param name="Output">The physical cooked output root.</param>
/// <param name="Layout">The loose cooked layout.</param>
/// <param name="Jobs">The manifest jobs.</param>
public sealed record ContentImportManifest(
    [property: JsonPropertyName("version")] int Version,
    [property: JsonPropertyName("output")] string Output,
    [property: JsonPropertyName("layout")] ContentImportLayout Layout,
    [property: JsonPropertyName("jobs")] IReadOnlyList<ContentImportJob> Jobs)
{
    /// <summary>Gets the fresh generation identity assigned by the publication owner.</summary>
    [JsonPropertyName("source_key")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public Guid? SourceKey { get; init; }

    /// <summary>Gets the worker threads of the import's CPU work pool; null for the ImportTool's default.</summary>
    [JsonPropertyName("thread_pool_size")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? ThreadPoolSize { get; init; }

    /// <summary>Gets the jobs processed at once; null for the ImportTool's default.</summary>
    [JsonPropertyName("max_in_flight_jobs")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? MaxInFlightJobs { get; init; }

    /// <summary>Gets the pipelines whose workers or queue capacity are set; null when none is.</summary>
    [JsonPropertyName("concurrency")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public IReadOnlyDictionary<string, ContentImportConcurrency>? Concurrency { get; init; }

    /// <summary>Gets the manifest with the execution settings of an import's run; settings left unset keep the
    /// ImportTool's defaults.</summary>
    /// <param name="run">The run's settings.</param>
    /// <returns>The manifest that runs with them.</returns>
    public ContentImportManifest WithRun(ImportRunSettings run)
    {
        ArgumentNullException.ThrowIfNull(run);
        var pipelines = run.Concurrency.Where(static pipeline => !pipeline.Value.IsEmpty)
            .ToDictionary(static pipeline => pipeline.Key, static pipeline => new ContentImportConcurrency(pipeline.Value.Workers, pipeline.Value.QueueCapacity), StringComparer.Ordinal);
        return this with
        {
            ThreadPoolSize = run.ThreadPoolSize,
            MaxInFlightJobs = run.MaxInFlightJobs,
            Concurrency = pipelines.Count == 0 ? null : pipelines,
        };
    }
}
