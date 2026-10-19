// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Import;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>Supplies facts for dependency-free material and scene orchestration scenarios.</summary>
internal static class ControlledSourceAnalysis
{
    public static Task<NativeSourceAnalysisReport> AnalyzeAsync(ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(new NativeSourceAnalysisReport(
            execution.Artifacts!.Fingerprint,
            Complete: true,
            [.. execution.Jobs.Select(job => Describe(execution.InputRoot, job))]));
    }

    private static NativeSourceAnalysisJob Describe(string root, ContentImportJob job)
    {
        var kind = job.Type switch
        {
            "material-descriptor" => "material",
            "scene-descriptor" => "scene",
            _ => throw new InvalidOperationException($"Provide explicit dependency facts for '{job.Type}'."),
        };
        var source = Path.GetFullPath(job.AttributedSource, root);
        var metadata = CookSavedSourceReader.ReadMetadata(source);
        var digest = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source)));

        // A single-asset job's output is the identity it names.
        var output = job.VirtualPath ?? throw new InvalidOperationException($"Job '{job.Id}' names no virtual path.");
        return new(job.Id, job.Type, source, Complete: true,
            [new(output, kind, "/", Required: true)],
            References: [],
            [new(source, Required: true)],
            [new(source, Exists: true, metadata, [new(0, 0, digest)])],
            Diagnostics: []);
    }
}
