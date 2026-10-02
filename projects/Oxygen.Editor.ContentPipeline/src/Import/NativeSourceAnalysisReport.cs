// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Native dependency facts bound to the verified producer that computed them.</summary>
/// <param name="ProducerFingerprint">The owning artifact lease's content identity.</param>
/// <param name="Complete">Whether every requested job was analyzed successfully.</param>
/// <param name="Jobs">Source-attributed dependencies, observations and diagnostics.</param>
public sealed record NativeSourceAnalysisReport(string ProducerFingerprint, bool Complete, ImmutableArray<NativeSourceAnalysisJob> Jobs)
{
    /// <summary>Validates and detaches native facts; the report's display version is not a cache identity.</summary>
    /// <param name="json">The native report bytes decoded as UTF-8.</param>
    /// <param name="operationId">The owning operation's diagnostic identity.</param>
    /// <param name="producerFingerprint">The verified artifact lease fingerprint.</param>
    /// <param name="schema">The source-analysis schema protected by the artifact lease.</param>
    /// <returns>Immutable native facts ready for managed project resolution and capture.</returns>
    public static NativeSourceAnalysisReport Parse(string json, Guid operationId, string producerFingerprint, JsonSchema schema)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(producerFingerprint);
        ArgumentNullException.ThrowIfNull(schema);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (!schema.Evaluate(root).IsValid)
        {
            throw new InvalidDataException("The engine returned an invalid source-analysis report.");
        }

        var jobs = root.GetProperty("jobs").EnumerateArray().Select(item => ReadJob(item, operationId)).ToImmutableArray();
        var complete = root.GetProperty("complete").GetBoolean();
        if ((complete && jobs.Any(static job => !job.Complete))
            || jobs.Any(static job => job.Complete && job.Diagnostics.Any(static issue => issue.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal)))
        {
            throw new InvalidDataException("The native source-analysis completion state contradicts its job results.");
        }

        return new(producerFingerprint, complete, jobs);
    }

    private static NativeSourceAnalysisJob ReadJob(JsonElement item, Guid operationId)
        => new(
            item.GetProperty("id").GetString()!,
            item.GetProperty("job_type").GetString()!,
            item.GetProperty("source_path").GetString()!,
            item.GetProperty("complete").GetBoolean(),
            ReadLogical(item.GetProperty("outputs")),
            ReadLogical(item.GetProperty("references")),
            item.GetProperty("files").EnumerateArray().Select(static file => new NativeSourceFileDependency(
                ReadPath(file.GetProperty("path")),
                file.GetProperty("required").GetBoolean())).ToImmutableArray(),
            item.GetProperty("observations").EnumerateArray().Select(ReadObservation).ToImmutableArray(),
            item.GetProperty("diagnostics").EnumerateArray().Select(issue => new DiagnosticRecord
            {
                OperationId = operationId,
                Domain = FailureDomain.AssetImport,
                Severity = issue.GetProperty("severity").GetString() switch
                {
                    "Info" => DiagnosticSeverity.Info,
                    "Warning" => DiagnosticSeverity.Warning,
                    _ => DiagnosticSeverity.Error,
                },
                Code = issue.GetProperty("code").GetString()!,
                Message = issue.GetProperty("message").GetString()!,
                AffectedPath = issue.GetProperty("source_path").GetString(),
                TechnicalMessage = issue.GetProperty("object_path").GetString(),
            }).ToImmutableArray());

    private static ImmutableArray<NativeLogicalDependency> ReadLogical(JsonElement entries)
        => entries.EnumerateArray().Select(static item => new NativeLogicalDependency(
            item.GetProperty("virtual_path").GetString()!,
            item.GetProperty("kind").GetString()!,
            item.GetProperty("object_path").GetString()!,
            item.GetProperty("required").GetBoolean())).ToImmutableArray();

    private static NativeSourceObservation ReadObservation(JsonElement item)
    {
        var observation = new NativeSourceObservation(
            ReadPath(item.GetProperty("path")),
            item.GetProperty("exists").GetBoolean(),
            ReadMetadata(item.GetProperty("metadata")),
            item.GetProperty("reads").EnumerateArray().Select(static read => new NativeSourceReadProof(
                read.GetProperty("offset").GetUInt64(),
                read.GetProperty("max_bytes").GetUInt64(),
                read.GetProperty("sha256").GetString()!)).ToImmutableArray());
        if (!observation.Exists && (observation.Metadata is not null || !observation.Reads.IsEmpty))
        {
            throw new InvalidDataException("An absent native source observation contains metadata or consumed bytes.");
        }

        return observation;
    }

    private static NativeSourceFileMetadata? ReadMetadata(JsonElement metadata)
        => metadata.ValueKind is JsonValueKind.Null ? null : new(
            metadata.GetProperty("size").GetUInt64(),
            metadata.GetProperty("is_directory").GetBoolean(),
            metadata.GetProperty("is_symlink").GetBoolean(),
            metadata.GetProperty("last_modified_seconds").GetInt64(),
            metadata.GetProperty("last_modified_nanoseconds").GetInt32());

    private static string ReadPath(JsonElement item)
    {
        var path = item.GetString()!;
        return Path.IsPathFullyQualified(path) && !path.Contains('\0', StringComparison.Ordinal)
            ? Path.GetFullPath(path) : throw new InvalidDataException("Native source observations require absolute file paths.");
    }
}
