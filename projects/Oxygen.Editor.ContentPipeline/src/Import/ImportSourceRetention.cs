// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Retains a complete discovered source bundle under the existing project writer and coherent-input contract.</summary>
/// <param name="documents">Existing document owners and saved-input gates.</param>
/// <param name="coordinator">The project writer and activation lifetime.</param>
public sealed class ImportSourceRetention(ICookDocumentRegistry documents, IContentCookCoordinator coordinator)
{
    /// <summary>Captures all discovered dependencies, then installs the new bundle without overwriting an existing source.</summary>
    /// <param name="operation">The operation owning the project writer.</param>
    /// <param name="bundleName">One directory name beneath SourceMedia.</param>
    /// <param name="primaryRelativePath">The selected source path within the discovered bundle.</param>
    /// <param name="discover">Complete source/dependency discovery, including hashes and preserved relative paths.</param>
    /// <param name="cancellationToken">Cancels capture before the bundle is installed.</param>
    /// <returns>The retained portable source facts or the participating document blockers.</returns>
    public async Task<ImportSourceRetentionResult> RetainAsync(
        ContentCookOperation operation,
        string bundleName,
        string primaryRelativePath,
        Func<CancellationToken, Task<IReadOnlyList<CookSnapshotInput>>> discover,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(discover);
        ArgumentException.ThrowIfNullOrWhiteSpace(primaryRelativePath);
        cancellationToken.ThrowIfCancellationRequested();
        coordinator.VerifyWriter(operation);
        var destination = ResolveDestination(operation.Project.ProjectRoot, bundleName);
        EnsureAvailable(destination);
        var primary = primaryRelativePath.Replace('\\', '/');
        var capture = new CookInputSnapshotCapture(documents, coordinator);
        var result = await capture.CaptureAsync(
            operation,
            async token =>
            {
                var files = await discover(token).ConfigureAwait(false);
                return files.Any(static file => file.IsAbsent)
                    || !files.Any(file => string.Equals(file.RelativePath.Replace('\\', '/'), primary, StringComparison.OrdinalIgnoreCase))
                    ? throw new InvalidDataException("Source retention requires the selected file and every discovered dependency to be present.")
                    : files;
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

    private static string ResolveDestination(string projectRoot, string bundleName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(bundleName);
        if (bundleName is "." or ".." || bundleName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || bundleName.Contains('/', StringComparison.Ordinal) || bundleName.Contains('\\', StringComparison.Ordinal)
            || bundleName.EndsWith('.') || bundleName.EndsWith(' '))
        {
            throw new ArgumentException("A source bundle needs one valid directory name beneath SourceMedia.", nameof(bundleName));
        }

        var sourceRoot = Path.Combine(Path.GetFullPath(projectRoot), "SourceMedia");
        CookOutputLease.RejectReparsePoint(sourceRoot);
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
