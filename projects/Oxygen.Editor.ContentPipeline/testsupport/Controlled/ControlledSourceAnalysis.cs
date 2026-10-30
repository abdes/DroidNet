// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>Supplies facts for dependency-free material, scene and script orchestration scenarios.</summary>
internal static class ControlledSourceAnalysis
{
    public static Task<NativeSourceAnalysisReport> AnalyzeAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(new NativeSourceAnalysisReport(
            Complete: true,
            [.. execution.Jobs.Select(job => Describe(execution, job))]));
    }

    private static NativeSourceAnalysisJob Describe(ContentSourceAnalysisExecution execution, ContentImportJob job)
    {
        var kind = job.Type switch
        {
            "material-descriptor" => "material",
            "scene-descriptor" => "scene",
            "script" => "script",
            _ => throw new InvalidOperationException($"Provide explicit dependency facts for '{job.Type}'."),
        };
        // The tool reads a mount path through the run's mounts file, as the ImportTool does.
        var source = execution.MountsFile.Resolve(execution.InputRoot, job.AttributedSource);
        var metadata = CookSavedSourceReader.ReadMetadata(source);
        var digest = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source)));

        // A single-asset job's output is the identity it names.
        var output = job.VirtualPath ?? throw new InvalidOperationException($"Job '{job.Id}' names no virtual path.");
        return new(
            job.Id,
            job.Type,
            source,
            Complete: true,
            [new(output, kind, "/", Required: true)],
            References: [],
            [new(source, Required: true)],
            [new(source, Exists: true, metadata, [new(0, 0, digest)])],
            Diagnostics: []);
    }
}
