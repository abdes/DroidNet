// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Retains a complete discovered source bundle under the existing project writer and coherent-input contract.</summary>
/// <param name="documents">Existing document owners and saved-input gates.</param>
/// <param name="coordinator">The project writer and activation lifetime.</param>
public sealed class ImportSourceRetention(ICookDocumentRegistry documents, IContentCookCoordinator coordinator)
{
    /// <summary>Captures all discovered dependencies, then installs the new bundle without overwriting an existing source.</summary>
    /// <param name="operation">The operation owning the project writer.</param>
    /// <param name="bundleName">One directory name beneath Content/SourceMedia/DCC.</param>
    /// <param name="discover">Complete source/dependency discovery, including hashes and preserved relative paths.</param>
    /// <param name="cancellationToken">Cancels capture before the bundle is installed.</param>
    /// <returns>The retained portable source facts or the participating document blockers.</returns>
    public async Task<ImportSourceRetentionResult> RetainAsync(
        ContentCookOperation operation,
        string bundleName,
        Func<CancellationToken, Task<ImportSourceBundle>> discover,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(discover);
        cancellationToken.ThrowIfCancellationRequested();
        coordinator.VerifyWriter(operation);
        var destination = ResolveDestination(operation.Project, bundleName);
        EnsureAvailable(destination);
        var ownership = CookOutputLease.AcquireOperation(operation.Project.ProjectRoot, operation.OperationId);
        Task? retainedDrain = null;
        try
        {
            string? primary = null;
            var capture = new CookInputSnapshotCapture(documents, coordinator);
            var result = await capture.CaptureAsync(
                operation,
                async token =>
                {
                    var bundle = await discover(token).ConfigureAwait(false);
                    primary = ValidateBundle(bundle);
                    return bundle.Files;
                },
                buildFingerprint: "Oxygen.SourceRetention/v1",
                cancellationToken).ConfigureAwait(false);
            if (result.Snapshot is not { } snapshot)
            {
                return new(Source: null, result.NeedsSave, result.ExternalChanges);
            }

            cancellationToken.ThrowIfCancellationRequested();
            coordinator.VerifyWriter(operation);
            EnsureAvailable(destination);
            var parent = Path.GetDirectoryName(destination)!;
            _ = Directory.CreateDirectory(parent);
            CookOutputLease.RejectReparsePoint(parent);
            Directory.Move(snapshot.InputRoot, destination);
            var retained = new RetainedImportSource(
                Path.GetRelativePath(operation.Project.ProjectRoot, destination).Replace('\\', '/'),
                snapshot.Inputs.Single(file => string.Equals(file.RelativePath, primary, StringComparison.OrdinalIgnoreCase)).RelativePath,
                snapshot.Inputs.Select(static file => new RetainedImportSourceFile(file.RelativePath, file.DiscoveryHash)).ToImmutableArray());
            return new(retained, [], []);
        }
        catch (ContentPipelineTerminationException failure)
        {
            retainedDrain = ReleaseAfterDrainAsync(failure.DrainCompletion, ownership);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, retainedDrain);
        }
        finally
        {
            if (retainedDrain is null)
            {
                await ownership.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static string ValidateBundle(ImportSourceBundle bundle)
    {
        if (string.IsNullOrWhiteSpace(bundle.PrimaryRelativePath))
        {
            throw new InvalidDataException("Source discovery did not identify its primary file.");
        }

        var primary = bundle.PrimaryRelativePath.Replace('\\', '/');
        return bundle.Files.Any(static file => file.IsAbsent)
            || !bundle.Files.Any(file => string.Equals(file.RelativePath.Replace('\\', '/'), primary, StringComparison.OrdinalIgnoreCase))
            ? throw new InvalidDataException("Source retention requires the selected file and every discovered dependency to be present.")
            : primary;
    }

    private static async Task ReleaseAfterDrainAsync(Task drain, FileStream ownership)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            await ownership.DisposeAsync().ConfigureAwait(false);
        }
    }

    private static string ResolveDestination(ProjectContext project, string bundleName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(bundleName);
        if (bundleName is "." or ".." || bundleName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || bundleName.Contains('/', StringComparison.Ordinal) || bundleName.Contains('\\', StringComparison.Ordinal)
            || bundleName.EndsWith('.') || bundleName.EndsWith(' '))
        {
            throw new ArgumentException("A source bundle needs one valid directory name beneath Content/SourceMedia/DCC.", nameof(bundleName));
        }

        var content = project.AuthoringMounts.FirstOrDefault(static mount => string.Equals(mount.Name, "Content", StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidOperationException("Source retention requires the project's Content authoring mount.");
        var projectRoot = Path.GetFullPath(project.ProjectRoot);
        var contentRoot = Path.GetFullPath(Path.Combine(projectRoot, content.RelativePath));
        var relative = Path.GetRelativePath(projectRoot, contentRoot).Replace('\\', '/');
        if (Path.IsPathRooted(relative) || string.Equals(relative, "..", StringComparison.Ordinal) || relative.StartsWith("../", StringComparison.Ordinal)
            || relative.Split('/')[0].ToUpperInvariant() is ".BUILD" or ".COOKED" or ".IMPORTED" or ".PIPELINE")
        {
            throw new InvalidOperationException("The Content authoring mount must remain inside the project and outside derived output.");
        }

        var sourceRoot = projectRoot;
        foreach (var segment in relative.Split('/').Where(static segment => !string.Equals(segment, ".", StringComparison.Ordinal)).Concat(["SourceMedia", "DCC"]))
        {
            sourceRoot = Path.Combine(sourceRoot, segment);
            CookOutputLease.RejectReparsePoint(sourceRoot);
        }

        return Path.Combine(sourceRoot, bundleName);
    }

    private static void EnsureAvailable(string destination)
    {
        CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(destination)!);
        CookOutputLease.RejectReparsePoint(destination);
        if (Directory.Exists(destination) || File.Exists(destination))
        {
            throw new IOException($"Source destination '{destination}' already exists. Choose another name or reimport the existing source.");
        }
    }
}
