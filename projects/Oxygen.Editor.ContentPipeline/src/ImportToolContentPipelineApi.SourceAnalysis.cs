// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Runs native dependency frontiers under the cook's artifact and worker ownership.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    /// <inheritdoc />
    public async Task<NativeSourceAnalysisReport> AnalyzeSourcesAsync(
        ContentSourceAnalysisExecution execution, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(execution);
        ArgumentNullException.ThrowIfNull(execution.Jobs, nameof(execution));
        if (execution.OperationId == Guid.Empty || execution.Jobs.Count == 0
            || !Path.IsPathFullyQualified(execution.InputRoot)
            || !Path.IsPathFullyQualified(execution.OperationRoot))
        {
            throw new ArgumentException("Native source analysis requires an operation identity, absolute roots and at least one job.", nameof(execution));
        }

        cancellationToken.ThrowIfCancellationRequested();
        var compatibility = execution.Artifacts is { } borrowed
            ? new NativeCompatibilityResult(borrowed, [])
            : await this.nativeCompatibility.VerifyAsync(execution.OperationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            throw new NativeCompatibilityException(compatibility.Diagnostics);
        }

        var artifacts = compatibility.Artifacts!;
        var files = RunFiles.For(Path.Combine(execution.OperationRoot, "manifests", $"analysis-{Guid.NewGuid():N}.json"));
        string[] ownedPaths = [files.Manifest, files.Report, files.Mounts, files.Captures];
        Task? retainedWorkerDrain = null;
        try
        {
            return await this.RunSourceAnalysisWorkerAsync(execution, artifacts, files, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException error)
        {
            retainedWorkerDrain = ReleaseAfterWorkerDrainAsync(error.DrainCompletion, ownedPaths, execution.Artifacts is null ? artifacts : null);
            throw new ContentPipelineTerminationException(error.InnerException ?? error, retainedWorkerDrain);
        }
        finally
        {
            if (retainedWorkerDrain is null)
            {
                if (execution.Artifacts is null)
                {
                    await artifacts.DisposeAsync().ConfigureAwait(false);
                }

                foreach (var path in ownedPaths)
                {
                    TryDeleteFile(path);
                }
            }
        }
    }

    private static void ValidateSourceAnalysisReport(ContentSourceAnalysisExecution execution, int exitCode, NativeSourceAnalysisReport report)
    {
        if (report.Jobs.Length != execution.Jobs.Count
            || (exitCode != 0 && report.Complete))
        {
            throw new InvalidDataException("The native source-analysis report does not match the submitted jobs or process result.");
        }

        for (var index = 0; index < report.Jobs.Length; ++index)
        {
            var observed = report.Jobs[index];
            var submitted = execution.Jobs[index];
            if (!string.Equals(observed.Id, submitted.Id, StringComparison.Ordinal)
                || !string.Equals(observed.JobType, submitted.ReportedType, StringComparison.Ordinal)
                || !Path.IsPathFullyQualified(observed.SourcePath)
                || FileSystemPath.From(observed.SourcePath) != FileSystemPath.From(execution.MountsFile.Resolve(execution.InputRoot, submitted.AttributedSource)))
            {
                throw new InvalidDataException("The native source-analysis report does not match the submitted job identities.");
            }
        }
    }

    private async Task<NativeSourceAnalysisReport> RunSourceAnalysisWorkerAsync(
        ContentSourceAnalysisExecution execution, NativeArtifactLease artifacts, RunFiles files, CancellationToken cancellationToken)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(files.Manifest)!);
        var manifest = File.Create(files.Manifest);
        await using (manifest.ConfigureAwait(false))
        {
            var document = new Dictionary<string, object> { ["version"] = 1, ["jobs"] = execution.Jobs };
            if (execution.Mounts is { Count: > 0 } mounts)
            {
                document["mounts"] = mounts;
            }

            await JsonSerializer.SerializeAsync(manifest, document, ManifestJsonOptions, cancellationToken).ConfigureAwait(false);
        }

        await execution.MountsFile.WriteAsync(files.Mounts, cancellationToken).ConfigureAwait(false);
        var request = new ContentPipelineProcessRequest(
            this.GetCompatibleToolPath(artifacts),
            ["--no-tui", "--no-color", "--quiet", "analyze-sources", "--manifest", files.Manifest,
             "--root", execution.InputRoot, "--mounts", files.Mounts, "--report", files.Report],
            execution.OperationRoot)
        {
            Output = CookRunContext.Current is { } progress ? new CookOutput(progress) : null,
        };
        if (execution.CapturedInputs is { } inputs)
        {
            await WriteCapturedInputsAsync(inputs, files.Captures, cancellationToken).ConfigureAwait(false);
            request = request with { Arguments = [.. request.Arguments, "--captured-inputs", files.Captures] };
        }

        var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
        if (!File.Exists(files.Report))
        {
            throw new InvalidDataException($"Native source analysis did not produce its required report. {result.StandardError} {result.StandardOutput}");
        }

        var report = NativeSourceAnalysisReport.Parse(
            await File.ReadAllTextAsync(files.Report, cancellationToken).ConfigureAwait(false),
            execution.OperationId,
            JsonSchema.FromFile(artifacts.GetPath(NativeArtifactInventory.SourceAnalysisSchemaId), new BuildOptions { SchemaRegistry = new SchemaRegistry() }));
        ValidateSourceAnalysisReport(execution, result.ExitCode, report);

        return report;
    }
}
