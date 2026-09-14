// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Uses the installed Inspector under the same native ownership boundary as cooking.</summary>
public sealed partial class ImportToolContentPipelineApi : ICookedDependencyInspector, ICookedAssetKeyProvider
{
    /// <inheritdoc />
    public Task<CookedDependencyReport> InspectDependenciesAsync(string operationRoot, string cookedRoot, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
        => this.RunInspectorAsync(operationRoot, ["dependencies", Path.GetFullPath(cookedRoot)], request: null, CookedDependencyReport.Parse, artifacts, cancellationToken);

    /// <inheritdoc />
    public async Task<CookedAssetKeyMap> ResolveAssetKeysAsync(string operationRoot, IReadOnlyList<string> virtualPaths, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        var request = JsonSerializer.Serialize(new { schema = "oxygen.asset-key-request.v1", virtual_paths = virtualPaths });
        var report = await this.RunInspectorAsync(operationRoot, ["asset-keys"], request, CookedAssetKeyMap.Parse, artifacts, cancellationToken).ConfigureAwait(false);
        report.ValidatePaths(virtualPaths);
        return report;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "Failed termination transfers the Inspector handle to the returned worker-drain continuation; ordinary completion awaits disposal in finally.")]
    private async Task<T> RunInspectorAsync<T>(string operationRoot, IReadOnlyList<string> arguments, string? request, Func<string, T> parse, NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        var compatibility = artifacts is not null ? new NativeCompatibilityResult(artifacts, [])
            : await this.nativeCompatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            throw new NativeCompatibilityException(compatibility.Diagnostics);
        }

        var compatible = compatibility.Artifacts!;
        var output = Path.Combine(Path.GetFullPath(operationRoot), "dependency-inspection", Guid.NewGuid().ToString("N") + ".json");
        var input = request is null ? null : output + ".request.json";
        FileStream? inspector = null;
        Task? drain = null;
        try
        {
            var tool = Path.Combine(Path.GetDirectoryName(this.GetCompatibleToolPath(compatible))!, "Oxygen.Cooker.Inspector.exe");
            inspector = new FileStream(tool, FileMode.Open, FileAccess.Read, FileShare.Read);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            var command = arguments.ToList();
            if (input is not null)
            {
                await File.WriteAllTextAsync(input, request, cancellationToken).ConfigureAwait(false);
                command.AddRange(["--input", input]);
            }

            command.AddRange(["--output", output]);
            var result = await this.processRunner.RunAsync(new(tool, command, Path.GetFullPath(operationRoot)), cancellationToken).ConfigureAwait(false);
            return result.ExitCode == 0
                ? parse(await File.ReadAllTextAsync(output, cancellationToken).ConfigureAwait(false))
                : throw new InvalidDataException($"Native content inspection failed: {result.StandardError} {result.StandardOutput}");
        }
        catch (ContentPipelineTerminationException failure)
        {
            drain = ReleaseAfterWorkerDrainAsync(failure.DrainCompletion, output, artifacts is null ? compatible : null, additionalPath: input, additionalLease: inspector);
            inspector = null;
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
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
                if (input is not null)
                {
                    TryDeleteFile(input);
                }
            }
        }
    }
}
