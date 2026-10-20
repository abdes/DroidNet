// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
/// The one mapping between the project's virtual paths and its files: each mount names a folder on disk, and a path
/// below the mount names the file or folder at the same place below that folder.
/// </summary>
public static class ProjectAssetFiles
{
    /// <summary>Finds the file or folder at a virtual path.</summary>
    /// <param name="project">The project.</param>
    /// <param name="path">The virtual path.</param>
    /// <returns>The location, or <see langword="null"/> when the path is not under one of the project's mounts.</returns>
    public static ProjectAssetLocation? Find(this ProjectContext project, AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(path);
        return Mounts(project).FirstOrDefault(mount => string.Equals(mount.Name, path.Mount, StringComparison.Ordinal)) is { } mount
            ? new(path, path.IsMount ? mount.Root : mount.Root.Combine(path.RelativePath), mount.Root, mount.Kind)
            : null;
    }

    /// <summary>
    /// Finds the source file of an asset: the first of <see cref="AssetFileTypes.SourcesOf"/> that exists under the
    /// project's mounts.
    /// </summary>
    /// <param name="project">The project.</param>
    /// <param name="identity">The asset's identity.</param>
    /// <returns>The source's location, or <see langword="null"/> when no source of the asset exists.</returns>
    public static ProjectAssetLocation? FindSource(this ProjectContext project, AssetPath identity)
        => FindSource(project, identity, File.Exists);

    /// <summary>
    /// Finds the source file of an asset: the first of <see cref="AssetFileTypes.SourcesOf"/> that exists under the
    /// project's mounts in a file system.
    /// </summary>
    /// <param name="project">The project.</param>
    /// <param name="identity">The asset's identity.</param>
    /// <param name="fileSystem">The file system that holds the project's files.</param>
    /// <returns>The source's location, or <see langword="null"/> when no source of the asset exists.</returns>
    public static ProjectAssetLocation? FindSource(this ProjectContext project, AssetPath identity, IFileSystem fileSystem)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        return FindSource(project, identity, fileSystem.File.Exists);
    }

    /// <summary>
    /// Locates the source file of an asset: its existing source, or else where its most specific source would be, so a
    /// caller reading it reports the missing file rather than an unknown asset.
    /// </summary>
    /// <param name="project">The project.</param>
    /// <param name="identity">The asset's identity.</param>
    /// <returns>The source's location, or <see langword="null"/> when the asset is not under one of the mounts.</returns>
    public static ProjectAssetLocation? LocateSource(this ProjectContext project, AssetPath identity)
        => project.FindSource(identity) ?? project.Find(AssetFileTypes.SourcesOf(identity)[0]);

    /// <summary>Finds the virtual path of a file or folder.</summary>
    /// <param name="project">The project.</param>
    /// <param name="file">The file or folder.</param>
    /// <returns>
    /// The location, or <see langword="null"/> when the path is not under one of the mounts or has a name the virtual
    /// path rules refuse.
    /// </returns>
    /// <remarks>When mounts nest, the innermost mount holds the path.</remarks>
    public static ProjectAssetLocation? FindByFilePath(this ProjectContext project, FileSystemPath file)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(file);
        var holder = Mounts(project)
            .Where(mount => file.IsSameOrInside(mount.Root))
            .MaxBy(static mount => mount.Root.FullPath.Length);
        return holder is not null && AssetPath.TryCreate(holder.Name, file.GetPathBelow(holder.Root), out var virtualPath)
            ? new(virtualPath, file, holder.Root, holder.Kind)
            : null;
    }

    private static ProjectAssetLocation? FindSource(ProjectContext project, AssetPath identity, Func<string, bool> exists)
    {
        ArgumentNullException.ThrowIfNull(identity);
        return AssetFileTypes.SourcesOf(identity)
            .Select(source => project.Find(source))
            .FirstOrDefault(location => location is not null && exists(location.File.FullPath));
    }

    private static IEnumerable<Mount> Mounts(ProjectContext project)
    {
        var root = project.ProjectRoot;
        return project.AuthoringMounts
            .Select(mount => new Mount(mount.Name, root.Combine(mount.RelativePath), ProjectMountKind.Authoring))
            .Concat(project.LocalFolderMounts.Select(static mount => new Mount(mount.Name, mount.Root, ProjectMountKind.LocalFolder)));
    }

    private sealed record Mount(string Name, FileSystemPath Root, ProjectMountKind Kind);
}
