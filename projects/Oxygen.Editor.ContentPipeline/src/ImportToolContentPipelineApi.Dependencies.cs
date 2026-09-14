// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Uses the installed Inspector under the same native ownership boundary as cooking.</summary>
public sealed partial class ImportToolContentPipelineApi : ICookedDependencyInspector
{
    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "Failed termination transfers the Inspector handle to the worker-drain continuation; ordinary completion awaits disposal in finally.")]
    public async Task<CookedDependencyReport> InspectDependenciesAsync(string operationRoot, string cookedRoot, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        var compatibility = artifacts is not null ? new NativeCompatibilityResult(artifacts, [])
            : await this.nativeCompatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            throw new NativeCompatibilityException(compatibility.Diagnostics);
        }

        var compatible = compatibility.Artifacts!;
        var output = Path.Combine(Path.GetFullPath(operationRoot), "dependency-inspection", Guid.NewGuid().ToString("N") + ".json");
        FileStream? inspector = null;
        Task? drain = null;
        try
        {
            var tool = Path.Combine(Path.GetDirectoryName(this.GetCompatibleToolPath(compatible))!, "Oxygen.Cooker.Inspector.exe");
            inspector = new FileStream(tool, FileMode.Open, FileAccess.Read, FileShare.Read);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            var result = await this.processRunner.RunAsync(new(tool, ["dependencies", Path.GetFullPath(cookedRoot), "--output", output], Path.GetFullPath(operationRoot)), cancellationToken).ConfigureAwait(false);
            return result.ExitCode == 0
                ? CookedDependencyReport.Parse(await File.ReadAllTextAsync(output, cancellationToken).ConfigureAwait(false))
                : throw new InvalidDataException($"Cooked dependency inspection failed: {result.StandardError} {result.StandardOutput}");
        }
        catch (ContentPipelineTerminationException failure)
        {
            drain = failure.DrainCompletion;
            _ = ReleaseAfterWorkerDrainAsync(drain, output, artifacts is null ? compatible : null, additionalLease: inspector);
            inspector = null;
            throw;
        }
        finally
        {
            if (inspector is not null)
            {
                await inspector.DisposeAsync().ConfigureAwait(false);
            }

            if (drain is null)
            {
                if (artifacts is null)
                {
                    await compatible.DisposeAsync().ConfigureAwait(false);
                }

                TryDeleteFile(output);
            }
        }
    }
}
