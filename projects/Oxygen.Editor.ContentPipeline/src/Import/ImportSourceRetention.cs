// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Retains a complete discovered source bundle under the existing project writer and coherent-input contract.</summary>
/// <param name="coordinator">The project writer and activation lifetime.</param>
public sealed class ImportSourceRetention(IContentCookCoordinator coordinator)
{
    /// <summary>Resolves the bundle folder a model is imported into: <paramref name="bundleName"/> in the chosen folder.</summary>
    /// <param name="project">The project owning the source bundle.</param>
    /// <param name="folder">The authoring folder the user chose.</param>
    /// <param name="bundleName">The bundle folder's name.</param>
    /// <returns>The contained absolute bundle directory.</returns>
    public static string ResolveDestination(ProjectContext project, AssetPath folder, string bundleName)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(folder);
        ValidateBundleName(bundleName);
        var mount = project.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal))
            ?? throw new ArgumentException("Choose an authoring folder, not built-in or cooked content.", nameof(folder));
        var segments = folder.RelativePath.Length == 0 ? [] : folder.RelativePath.Split('/');
        return Path.Combine(ResolveInsideMount(project, mount, segments), bundleName);
    }

    /// <summary>Captures all discovered dependencies, then installs the new bundle without overwriting an existing source.</summary>
    /// <param name="operation">The operation owning the project writer.</param>
    /// <param name="folder">The authoring folder the user chose for the model.</param>
    /// <param name="bundleName">The bundle folder's name inside <paramref name="folder"/>.</param>
    /// <param name="discover">Complete source/dependency discovery, including hashes and preserved relative paths.</param>
    /// <param name="cancellationToken">Cancels capture before the bundle is installed.</param>
    /// <returns>The retained portable source facts.</returns>
    public async Task<RetainedImportSource> RetainAsync(
        ContentCookOperation operation,
        AssetPath folder,
        string bundleName,
        Func<CancellationToken, Task<ImportSourceBundle>> discover,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(operation);
        ArgumentNullException.ThrowIfNull(discover);
        cancellationToken.ThrowIfCancellationRequested();
        coordinator.VerifyWriter(operation);
        var destination = ResolveDestination(operation.Project, folder, bundleName);
        EnsureAvailable(destination);
        string? primary = null;
        var capture = new CookInputSnapshotCapture(coordinator);
        var snapshot = await capture.CaptureAsync(
            operation,
            async token =>
            {
                var bundle = await discover(token).ConfigureAwait(false);
                primary = ValidateBundle(bundle);
                return bundle.Files;
            },
            cancellationToken).ConfigureAwait(false);
        cancellationToken.ThrowIfCancellationRequested();
        coordinator.VerifyWriter(operation);
        EnsureAvailable(destination);
        var parent = Path.GetDirectoryName(destination)!;
        _ = Directory.CreateDirectory(parent);
        CookOutputLease.RejectReparsePoint(parent);
        Directory.Move(snapshot.InputRoot, destination);
        return new RetainedImportSource(
            Path.GetRelativePath(operation.Project.ProjectRoot.FullPath, destination).Replace('\\', '/'),
            snapshot.Inputs.Single(file => string.Equals(file.RelativePath, primary, FileSystemPath.NameComparison)).RelativePath,
            [.. snapshot.Inputs.Where(static file => file.Kind == CookSnapshotInputKind.File).Select(static file => new RetainedImportSourceFile(file.RelativePath, file.DiscoveryHash))]);
    }

    /// <summary>Resolves the folder of an existing retained bundle from its project-relative path.</summary>
    /// <param name="project">The project owning the source bundle.</param>
    /// <param name="bundleRoot">The bundle's project-relative folder, as its import record names it.</param>
    /// <returns>The contained absolute bundle directory.</returns>
    internal static string ResolveBundleRoot(ProjectContext project, string bundleRoot)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentException.ThrowIfNullOrWhiteSpace(bundleRoot);
        var path = project.ProjectRoot.Combine(bundleRoot);
        foreach (var mount in project.AuthoringMounts)
        {
            var root = project.ProjectRoot.Combine(mount.RelativePath);
            if (path != root && path.IsSameOrInside(root))
            {
                return ResolveInsideMount(project, mount, path.GetPathBelow(root).Split('/'));
            }
        }

        throw new InvalidDataException("A retained source bundle must be inside an authoring mount.");
    }

    internal static void ValidateBundleName(string bundleName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(bundleName);
        if (bundleName is "." or ".." || bundleName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || bundleName.Contains('/', StringComparison.Ordinal) || bundleName.Contains('\\', StringComparison.Ordinal)
            || bundleName.EndsWith('.') || bundleName.EndsWith(' '))
        {
            throw new ArgumentException("A source bundle needs one valid directory name.", nameof(bundleName));
        }
    }

    // Walks a mount's folders, refusing derived content (SourceMedia, .cooked) and reparse points.
    private static string ResolveInsideMount(ProjectContext project, ProjectMountPoint mount, IEnumerable<string> segments)
    {
        var root = project.ProjectRoot.Combine(mount.RelativePath).FullPath;
        var relative = Path.GetRelativePath(project.ProjectRoot.FullPath, root).Replace('\\', '/');
        if (Path.IsPathRooted(relative) || string.Equals(relative, "..", StringComparison.Ordinal) || relative.StartsWith("../", StringComparison.Ordinal)
            || ProjectOutputFolders.Contains(relative))
        {
            throw new InvalidOperationException("The authoring mount must remain inside the project and outside derived output.");
        }

        var path = root;
        CookOutputLease.RejectReparsePoint(path);
        foreach (var segment in segments.Where(static segment => segment.Length != 0))
        {
            if (VirtualPath.IsDerivedContent(segment))
            {
                throw new ArgumentException("Choose a content destination outside source-media and generated folders.", nameof(segments));
            }

            path = Path.Combine(path, segment);
            CookOutputLease.RejectReparsePoint(path);
        }

        return path;
    }

    private static string ValidateBundle(ImportSourceBundle bundle)
    {
        if (string.IsNullOrWhiteSpace(bundle.PrimaryRelativePath))
        {
            throw new InvalidDataException("Source discovery did not identify its primary file.");
        }

        var primary = bundle.PrimaryRelativePath.Replace('\\', '/');
        return !bundle.Files.Any(file => file.Kind == CookSnapshotInputKind.File
            && string.Equals(file.RelativePath.Replace('\\', '/'), primary, FileSystemPath.NameComparison))
            ? throw new InvalidDataException("Source retention requires the selected primary file.")
            : primary;
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
