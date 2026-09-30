// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
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
        var manifestPath = Path.Combine(execution.OperationRoot, "manifests", $"analysis-{Guid.NewGuid():N}.json");
        var reportPath = manifestPath + ".report.json";
        var capturePath = manifestPath + ".captures.json";
        string[] ownedPaths = [manifestPath, reportPath, capturePath];
        Task? retainedWorkerDrain = null;
        try
        {
            return await this.RunSourceAnalysisWorkerAsync(execution, artifacts, manifestPath, reportPath, capturePath, cancellationToken).ConfigureAwait(false);
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
                || !string.Equals(observed.JobType, submitted.Type, StringComparison.Ordinal)
                || !Path.IsPathFullyQualified(observed.SourcePath)
                || !string.Equals(Path.GetFullPath(observed.SourcePath), Path.GetFullPath(submitted.Source, execution.InputRoot), StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("The native source-analysis report does not match the submitted job identities.");
            }
        }
    }

    private async Task<NativeSourceAnalysisReport> RunSourceAnalysisWorkerAsync(
        ContentSourceAnalysisExecution execution, NativeArtifactLease artifacts, string manifestPath, string reportPath, string capturePath, CancellationToken cancellationToken)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
        var manifest = File.Create(manifestPath);
        await using (manifest.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(manifest, new { version = 1, jobs = execution.Jobs }, ManifestJsonOptions, cancellationToken).ConfigureAwait(false);
        }

        var request = new ContentPipelineProcessRequest(
            this.GetCompatibleToolPath(artifacts),
            ["--no-tui", "--no-color", "--quiet", "analyze-sources", "--manifest", manifestPath,
             "--root", execution.InputRoot, "--report", reportPath],
            execution.OperationRoot)
        {
            Output = CookRunContext.Current is { } progress ? new CookOutput(progress) : null,
        };
        if (execution.CapturedInputs is { } inputs)
        {
            await WriteCapturedInputsAsync(inputs, capturePath, cancellationToken).ConfigureAwait(false);
            request = request with { Arguments = [.. request.Arguments, "--captured-inputs", capturePath] };
        }

        var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
        if (!File.Exists(reportPath))
        {
            throw new InvalidDataException($"Native source analysis did not produce its required report. {result.StandardError} {result.StandardOutput}");
        }

        var report = NativeSourceAnalysisReport.Parse(
            await File.ReadAllTextAsync(reportPath, cancellationToken).ConfigureAwait(false),
            execution.OperationId,
            artifacts.Fingerprint,
            JsonSchema.FromFile(artifacts.GetPath(NativeArtifactInventory.SourceAnalysisSchemaId), new BuildOptions { SchemaRegistry = new SchemaRegistry() }));
        ValidateSourceAnalysisReport(execution, result.ExitCode, report);

        return report;
    }
}
