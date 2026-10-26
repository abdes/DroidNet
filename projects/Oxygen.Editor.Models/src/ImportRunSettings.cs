// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Editor.Models;

/// <summary>The ImportTool's execution settings of one import: its work pool, the jobs it runs at once and each
/// pipeline's workers and queue capacity, in the top-level vocabulary of an import manifest.</summary>
/// <remarks>
/// They belong to an import, not to a model: no model import record keeps them, and an import session saves them. An
/// unset value takes the importer's default, which <see cref="Defaults"/> reads from the installed manifest schema.
/// </remarks>
public sealed record ImportRunSettings
{
    /// <summary>Gets the ImportTool's pipelines, in the manifest schema's order.</summary>
    public static ImmutableArray<string> Pipelines { get; } = ["texture", "buffer", "material", "mesh_build", "geometry", "scene"];

    /// <summary>Gets run settings that set nothing.</summary>
    public static ImportRunSettings None { get; } = new();

    /// <summary>Gets the worker threads of the import's CPU work pool, or <see langword="null"/> for the default.</summary>
    public int? ThreadPoolSize { get; init; }

    /// <summary>Gets the jobs processed at once, or <see langword="null"/> for the default.</summary>
    public int? MaxInFlightJobs { get; init; }

    /// <summary>Gets the pipelines whose workers or queue capacity are set, by pipeline name.</summary>
    public ImmutableDictionary<string, ImportPipelineSettings> Concurrency { get; init; }
        = ImmutableDictionary<string, ImportPipelineSettings>.Empty.WithComparers(StringComparer.Ordinal);

    /// <summary>Gets a value indicating whether the settings set nothing.</summary>
    public bool IsEmpty => this.ThreadPoolSize is null && this.MaxInFlightJobs is null && this.Concurrency.Values.All(static pipeline => pipeline.IsEmpty);

    /// <summary>Gets the importer's defaults: every pipeline's from the installed manifest schema, and the processor count
    /// for the work pool and the jobs at once, as the schema documents them.</summary>
    /// <returns>Settings that set every value to its default.</returns>
    public static ImportRunSettings Defaults()
    {
        var concurrency = ImportManifestSchema.Read()["properties"]!["concurrency"]!["properties"]!;
        return new()
        {
            ThreadPoolSize = Environment.ProcessorCount,
            MaxInFlightJobs = Environment.ProcessorCount,
            Concurrency = Pipelines.ToImmutableDictionary(
                static name => name,
                name => new ImportPipelineSettings(
                    concurrency[name]!["properties"]!["workers"]!["default"]!.GetValue<int>(),
                    concurrency[name]!["properties"]!["queue_capacity"]!["default"]!.GetValue<int>()),
                StringComparer.Ordinal),
        };
    }

    /// <summary>Reads the run settings of an import manifest; its other content is ignored.</summary>
    /// <param name="manifest">The manifest's root, which the manifest schema validated.</param>
    /// <returns>The settings the manifest sets.</returns>
    public static ImportRunSettings Read(JsonElement manifest)
    {
        var concurrency = ImmutableDictionary.CreateBuilder<string, ImportPipelineSettings>(StringComparer.Ordinal);
        if (manifest.TryGetProperty("concurrency", out var pipelines))
        {
            foreach (var pipeline in pipelines.EnumerateObject())
            {
                concurrency[pipeline.Name] = new(ReadInt(pipeline.Value, "workers"), ReadInt(pipeline.Value, "queue_capacity"));
            }
        }

        return new()
        {
            ThreadPoolSize = ReadInt(manifest, "thread_pool_size"),
            MaxInFlightJobs = ReadInt(manifest, "max_in_flight_jobs"),
            Concurrency = concurrency.ToImmutable(),
        };
    }

    /// <summary>Gets the settings with one pipeline's values replaced; a pipeline that sets neither is removed.</summary>
    /// <param name="pipeline">The pipeline's name, one of <see cref="Pipelines"/>.</param>
    /// <param name="settings">Its settings.</param>
    /// <returns>The changed settings.</returns>
    public ImportRunSettings WithPipeline(string pipeline, ImportPipelineSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        return Pipelines.Contains(pipeline, StringComparer.Ordinal)
            ? this with { Concurrency = settings.IsEmpty ? this.Concurrency.Remove(pipeline) : this.Concurrency.SetItem(pipeline, settings) }
            : throw new ArgumentException($"'{pipeline}' is not an ImportTool pipeline.", nameof(pipeline));
    }

    /// <summary>Writes the set values as top-level properties of an import manifest; unset values are left out.</summary>
    /// <param name="writer">A writer inside the manifest's root object.</param>
    public void WriteTo(Utf8JsonWriter writer)
    {
        ArgumentNullException.ThrowIfNull(writer);
        if (this.ThreadPoolSize is { } threads)
        {
            writer.WriteNumber("thread_pool_size", threads);
        }

        if (this.MaxInFlightJobs is { } jobs)
        {
            writer.WriteNumber("max_in_flight_jobs", jobs);
        }

        var set = Pipelines.Where(name => this.Concurrency.TryGetValue(name, out var pipeline) && !pipeline.IsEmpty).ToArray();
        if (set.Length == 0)
        {
            return;
        }

        writer.WriteStartObject("concurrency");
        foreach (var name in set)
        {
            var pipeline = this.Concurrency[name];
            writer.WriteStartObject(name);
            if (pipeline.Workers is { } workers)
            {
                writer.WriteNumber("workers", workers);
            }

            if (pipeline.QueueCapacity is { } capacity)
            {
                writer.WriteNumber("queue_capacity", capacity);
            }

            writer.WriteEndObject();
        }

        writer.WriteEndObject();
    }

    private static int? ReadInt(JsonElement element, string name)
        => element.TryGetProperty(name, out var value) ? value.GetInt32() : null;
}
