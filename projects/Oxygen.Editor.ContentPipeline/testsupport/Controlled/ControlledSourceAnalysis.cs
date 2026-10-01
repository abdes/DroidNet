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
        var (kind, extension, folder) = job.Type switch
        {
            "material-descriptor" => ("material", ".omat", job.Layout!.MaterialsDirectory),
            "scene-descriptor" => ("scene", ".oscene", job.Layout!.ScenesDirectory),
            _ => throw new InvalidOperationException($"Provide explicit dependency facts for '{job.Type}'."),
        };
        var source = Path.GetFullPath(job.Source, root);
        var metadata = CookSavedSourceReader.ReadMetadata(source);
        var digest = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source)));
        var output = string.Join('/', new[] { job.Layout!.VirtualMountRoot.TrimEnd('/'), folder?.Trim('/'), job.Name + extension }.Where(static part => !string.IsNullOrEmpty(part)));
        return new(job.Id, job.Type, source, Complete: true,
            [new(output, kind, "/", Required: true)],
            References: [],
            [new(source, Required: true)],
            [new(source, Exists: true, metadata, [new(0, 0, digest)])],
            Diagnostics: []);
    }
}
