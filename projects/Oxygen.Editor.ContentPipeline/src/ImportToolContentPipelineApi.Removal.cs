// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Removes cooked outputs under the same worker lifetime contract as import.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    /// <inheritdoc />
    public async Task RemoveCookedOutputsAsync(string operationRoot, string cookedRoot, Guid sourceKey, CookedOutputRemoval removal, NativeArtifactLease artifacts, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(cookedRoot);
        ArgumentNullException.ThrowIfNull(removal);
        ArgumentNullException.ThrowIfNull(artifacts);
        if (removal.IsEmpty)
        {
            throw new ArgumentException("A removal must list at least one cooked output.", nameof(removal));
        }

        cancellationToken.ThrowIfCancellationRequested();
        var input = Path.Combine(Path.GetFullPath(operationRoot), "removals", $"remove-{Guid.NewGuid():N}.json");
        Task? retainedWorkerDrain = null;
        try
        {
            var toolPath = this.GetCompatibleToolPath(artifacts);
            _ = Directory.CreateDirectory(Path.GetDirectoryName(input)!);
            await File.WriteAllTextAsync(
                input,
                JsonSerializer.Serialize(new { source_key = sourceKey, virtual_paths = removal.VirtualPaths, auxiliary_files = removal.AuxiliaryFiles }),
                cancellationToken).ConfigureAwait(false);
            var request = new ContentPipelineProcessRequest(
                toolPath, ["--no-tui", "--no-color", "--quiet", "remove-cooked", Path.GetFullPath(cookedRoot), "--input", input], Path.GetFullPath(operationRoot));
            var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
            if (result.ExitCode != 0)
            {
                throw new InvalidDataException($"The engine could not remove cooked outputs from '{cookedRoot}': {result.StandardError} {result.StandardOutput}");
            }
        }
        catch (ContentPipelineTerminationException exception)
        {
            retainedWorkerDrain = ReleaseAfterWorkerDrainAsync(exception.DrainCompletion, [input], artifacts: null);
            throw new ContentPipelineTerminationException(exception.InnerException ?? exception, retainedWorkerDrain);
        }
        finally
        {
            if (retainedWorkerDrain is null)
            {
                TryDeleteFile(input);
            }
        }
    }
}
