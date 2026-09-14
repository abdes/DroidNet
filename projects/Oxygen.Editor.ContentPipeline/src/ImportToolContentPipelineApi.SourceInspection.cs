// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads source metadata under the same owned-worker and artifact contract as cooking.</summary>
public sealed partial class ImportToolContentPipelineApi : ISceneSourceInspector
{
    private static readonly Lazy<EditorSchemaCatalog> SourceInspectionSchemas = new(
        static () => EditorSchemaCatalog.LoadFromDirectory(Path.Combine(AppContext.BaseDirectory, "Schemas")));

    /// <inheritdoc />
    public async Task<SceneSourceInspectionReport> InspectSceneSourceAsync(
        Guid operationId,
        string operationRoot,
        string sourcePath,
        CancellationToken cancellationToken,
        NativeArtifactLease? artifacts = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePath);
        cancellationToken.ThrowIfCancellationRequested();
        var compatibility = artifacts is not null
            ? new NativeCompatibilityResult(artifacts, [])
            : await this.nativeCompatibility.VerifyAsync(operationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            throw new NativeCompatibilityException(compatibility.Diagnostics);
        }

        var compatible = compatibility.Artifacts!;
        var output = Path.Combine(Path.GetFullPath(operationRoot), "source-inspection", $"report-{Guid.NewGuid():N}.json");
        Task? retainedWorkerDrain = null;
        try
        {
            _ = Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            var request = new ContentPipelineProcessRequest(
                this.GetCompatibleToolPath(compatible),
                ["--no-tui", "--no-color", "--quiet", "inspect-source", Path.GetFullPath(sourcePath), "--output", output],
                Path.GetFullPath(operationRoot));
            var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
            if (result.ExitCode != 0)
            {
                throw new InvalidOperationException($"Source inspection failed: {result.StandardError} {result.StandardOutput}");
            }

            var json = await File.ReadAllTextAsync(output, cancellationToken).ConfigureAwait(false);
            return SceneSourceInspectionReport.Parse(json, operationId, SourceInspectionSchemas.Value);
        }
        catch (ContentPipelineTerminationException exception)
        {
            retainedWorkerDrain = exception.DrainCompletion;
            throw;
        }
        finally
        {
            if (retainedWorkerDrain is null)
            {
                if (artifacts is null)
                {
                    await compatible.DisposeAsync().ConfigureAwait(false);
                }

                TryDeleteFile(output);
            }
            else
            {
                _ = ReleaseAfterWorkerDrainAsync(retainedWorkerDrain, output, artifacts is null ? compatible : null);
            }
        }
    }
}
