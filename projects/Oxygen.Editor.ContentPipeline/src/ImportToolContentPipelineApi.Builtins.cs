// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Queries the native procedural catalog under the same worker lifetime contract as import.</summary>
public sealed partial class ImportToolContentPipelineApi : IBuiltinGeometryCatalogProvider
{
    /// <inheritdoc/>
    public async Task<BuiltinGeometryCatalog> GetBuiltinGeometryCatalogAsync(string projectRoot, string mountName, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(mountName);
        cancellationToken.ThrowIfCancellationRequested();
        var toolPath = this.toolLocator.GetImportToolPath();
        var output = Path.Combine(projectRoot, ".pipeline", "Catalogs", $"builtins-{Guid.NewGuid():N}.json");
        Task? retainedWorkerDrain = null;
        try
        {
            var request = new ContentPipelineProcessRequest(
                toolPath, ["--no-tui", "--no-color", "--quiet", "builtin-catalog", output, "--mount", mountName], projectRoot);
            var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
            if (result.ExitCode != 0)
            {
                throw new InvalidOperationException($"The engine could not provide its built-in geometry catalog: {result.StandardError} {result.StandardOutput}");
            }

            var catalog = BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(output, cancellationToken).ConfigureAwait(false));
            return string.Equals(catalog.MountName, mountName, StringComparison.Ordinal)
                ? catalog : throw new InvalidDataException("The engine builtin catalog addresses a different output mount.");
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
                TryDeleteFile(output);
            }
            else
            {
                _ = DeleteAfterWorkerDrainAsync(retainedWorkerDrain, output);
            }
        }
    }
}
