// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>Maps the project's assets to their files under its mounts, and back.</summary>
/// <remarks>
/// <para>
/// The mounts are the project's authoring mounts and its local folder mounts. The mapping rejects an unknown mount, a
/// path that leaves its mount, and <c>.</c>, <c>..</c> and empty segments, and it writes asset URIs in the asset
/// catalog's form, so the URI of a file is the same wherever it is computed.
/// </para>
/// <para>
/// It knows no file formats or asset kinds: a caller that works on authoring mounts only, or on files only, checks
/// <see cref="ProjectAssetLocation.MountKind"/> or <see cref="ProjectAssetLocation.IsMountRoot"/>.
/// </para>
/// </remarks>
public static class ProjectAssetFiles
{
    /// <summary>Finds where an asset is.</summary>
    /// <param name="project">The project.</param>
    /// <param name="assetUri">The asset URI.</param>
    /// <returns>The location, or <see langword="null"/> when the URI does not name a place under one of the mounts.</returns>
    public static ProjectAssetLocation? FindByAssetUri(this ProjectContext project, Uri assetUri)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(assetUri);
        return string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            ? Locate(project, AssetUriHelper.GetMountPoint(assetUri), AssetUriHelper.GetRelativePath(assetUri))
            : null;
    }

    /// <summary>Finds where the asset at a virtual path is.</summary>
    /// <param name="project">The project.</param>
    /// <param name="virtualPath">The virtual path: the mount name, then the path inside the mount.</param>
    /// <returns>The location, or <see langword="null"/> when the path does not name a place under one of the mounts.</returns>
    public static ProjectAssetLocation? FindByVirtualPath(this ProjectContext project, string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(virtualPath);
        if (!virtualPath.StartsWith('/'))
        {
            return null;
        }

        var slash = virtualPath.IndexOf('/', 1);
        return slash < 0
            ? Locate(project, virtualPath[1..], string.Empty)
            : Locate(project, virtualPath[1..slash], virtualPath[(slash + 1)..]);
    }

    /// <summary>Finds which asset a file or folder is.</summary>
    /// <param name="project">The project.</param>
    /// <param name="path">The file or folder's path.</param>
    /// <returns>The location, or <see langword="null"/> when the path is not under one of the mounts.</returns>
    /// <remarks>When mounts nest, the innermost mount holds the path.</remarks>
    public static ProjectAssetLocation? FindByFilePath(this ProjectContext project, string path)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        var holder = Mounts(project)
            .Where(mount => string.Equals(full, mount.Root, StringComparison.OrdinalIgnoreCase)
                || full.StartsWith(mount.Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            .MaxBy(static mount => mount.Root.Length);
        return holder is null
            ? null
            : CreateLocation(holder, full.Length == holder.Root.Length ? string.Empty : full[(holder.Root.Length + 1)..].Replace('\\', '/'));
    }

    private static ProjectAssetLocation? Locate(ProjectContext project, string mountName, string relativePath)
    {
        if (mountName.Length == 0
            || Mounts(project).FirstOrDefault(mount => string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase)) is not { } mount)
        {
            return null;
        }

        var relative = relativePath.Replace('\\', '/');
        if (relative.Length == 0)
        {
            return CreateLocation(mount, string.Empty);
        }

        if (relative.Split('/').Any(static segment => segment.Length == 0 || segment is "." or ".." || segment.Contains(':', StringComparison.Ordinal)))
        {
            return null;
        }

        var full = Path.GetFullPath(Path.Combine(mount.Root, relative));
        return full.StartsWith(mount.Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
            ? CreateLocation(mount, relative)
            : null;
    }

    private static ProjectAssetLocation CreateLocation(Mount mount, string relative)
        => new(
            AssetUriHelper.CreateUri(mount.Name, relative),
            relative.Length == 0 ? "/" + mount.Name : $"/{mount.Name}/{relative}",
            relative.Length == 0 ? mount.Root : Path.GetFullPath(Path.Combine(mount.Root, relative)),
            mount.Name,
            mount.Root,
            relative,
            mount.Kind);

    private static IEnumerable<Mount> Mounts(ProjectContext project)
        => project.AuthoringMounts
            .Select(mount => new Mount(mount.Name, RootOf(Path.Combine(project.ProjectRoot, mount.RelativePath)), ProjectMountKind.Authoring))
            .Concat(project.LocalFolderMounts.Select(mount => new Mount(mount.Name, RootOf(mount.AbsolutePath), ProjectMountKind.LocalFolder)));

    private static string RootOf(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));

    private sealed record Mount(string Name, string Root, ProjectMountKind Kind);
}
