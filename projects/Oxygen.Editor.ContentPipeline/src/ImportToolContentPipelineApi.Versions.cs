// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads the importer revisions of the installed ImportTool, once per installed tool.</summary>
public sealed partial class ImportToolContentPipelineApi
{
    private volatile InstalledRevisions? installedRevisions;

    /// <inheritdoc />
    public async Task<ImporterRevisions> ReadImporterRevisionsAsync(NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        // The installed tool's file identifies the revisions it reports, so a status read of an unchanged tool needs
        // neither the cooker's verification nor a worker.
        if (this.installedRevisions is { } cached
            && cached.Tool == IdentityOf(artifacts is not null ? this.GetCompatibleToolPath(artifacts) : this.toolLocator.GetImportToolPath()))
        {
            return cached.Revisions;
        }

        var compatibility = artifacts is not null ? new NativeCompatibilityResult(artifacts, [])
            : await this.nativeCompatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            throw new NativeCompatibilityException(compatibility.Diagnostics);
        }

        var compatible = compatibility.Artifacts!;
        try
        {
            // The lease keeps the tool from changing while it runs.
            var tool = this.GetCompatibleToolPath(compatible);
            var identity = IdentityOf(tool);
            var result = await this.processRunner.RunAsync(new(tool, ["versions", "--json"], Path.GetDirectoryName(tool)!), cancellationToken).ConfigureAwait(false);
            if (result.ExitCode != 0)
            {
                throw new InvalidDataException($"The ImportTool could not report its versions: {result.StandardError} {result.StandardOutput}");
            }

            var revisions = ImporterRevisions.Parse(result.StandardOutput);
            this.LogImporterRevisions(tool, revisions);
            this.installedRevisions = new(identity, revisions);
            return revisions;
        }
        finally
        {
            if (artifacts is null)
            {
                await compatible.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static (string FullPath, long Length, DateTime LastWriteTimeUtc) IdentityOf(string tool)
    {
        var file = new FileInfo(tool);
        return (file.FullName, file.Length, file.LastWriteTimeUtc);
    }

    private sealed record InstalledRevisions((string FullPath, long Length, DateTime LastWriteTimeUtc) Tool, ImporterRevisions Revisions);
}
