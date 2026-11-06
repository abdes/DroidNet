// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
/// The one mapping between the project's virtual paths and its files: each mount names a folder on disk, and a path
/// below the mount names the file or folder at the same place below that folder.
/// </summary>
/// <remarks>
/// Besides the authoring and local folder mounts the project declares, the <see cref="ProjectFilesMount"/> mount names
/// the project's root folder, so its files outside every other mount, such as its configuration and media, have a
/// virtual path too. A file inside another mount is named from that mount, the innermost one.
/// </remarks>
public static class ProjectAssetFiles
{
    /// <summary>The name of the mount of the project's own files: its root folder.</summary>
    public const string ProjectFilesMount = "project";

    /// <summary>
    /// Checks the name of a mount the project declares: an authored mount name, and none of the mounts every project
    /// already has, its own files' and the engine's content, whatever its case.
    /// </summary>
    /// <param name="name">The mount name.</param>
    /// <returns>Why the project may not declare the name, or <see langword="null"/> when it may.</returns>
    public static string? ValidateDeclaredMountName(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        return !ProjectMounts.IsValidName(name) ? "A mount name starts with a letter or '_', then letters, digits, '_' or '-'."
            : IsReservedMountName(name) ? "Every project already has a mount with this name; choose another."
            : null;
    }

    /// <summary>
    /// Tests whether a name is reserved, whatever its case: one of the mounts every project already has, or a Content
    /// Explorer node that shows an output folder.
    /// </summary>
    /// <param name="name">The mount name.</param>
    /// <returns>Whether the name is the project files', the engine content's, or an output folder node's.</returns>
    public static bool IsReservedMountName(string name)
        => VirtualPathComparer.CaseFolded.Equals(name, ProjectFilesMount) || VirtualPathComparer.CaseFolded.Equals(name, AssetPath.Engine.Mount)
           || VirtualPathComparer.CaseFolded.Equals(name, ProjectOutputFolders.CookedViewName)
           || VirtualPathComparer.CaseFolded.Equals(name, ProjectOutputFolders.BuildViewName);

    /// <summary>Finds the file or folder at a virtual path.</summary>
    /// <param name="project">The project.</param>
    /// <param name="path">The virtual path.</param>
    /// <returns>The location, or <see langword="null"/> when the path is not under one of the project's mounts.</returns>
    public static ProjectAssetLocation? Find(this ProjectContext project, AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(path);
        if (Mounts(project).FirstOrDefault(mount => string.Equals(mount.Name, path.Mount, StringComparison.Ordinal)) is not { } mount)
        {
            return null;
        }

        if (path.IsMount)
        {
            return new(path, mount.Root, mount.Root, mount.Kind);
        }

        // A library's files are named by its index, not by its virtual paths; what a pak holds lies in the pak.
        if (mount.Library is not { } library)
        {
            return new(path, mount.Root.Combine(path.RelativePath), mount.Root, mount.Kind);
        }

        return !library.Holds(path.VirtualPath) ? null
            : library.DescriptorOf(path.VirtualPath) is { } descriptor ? new(path, mount.Root.Combine(descriptor), mount.Root, mount.Kind)
            : new(path, mount.Root, mount.Root, mount.Kind);
    }

    /// <summary>
    /// Finds the source file of an asset: the first of <see cref="AssetFileTypes.SourcesOf"/> that exists under the
    /// project's mounts.
    /// </summary>
    /// <param name="project">The project, whose file system holds its files.</param>
    /// <param name="identity">The asset's identity.</param>
    /// <returns>The source's location, or <see langword="null"/> when no source of the asset exists.</returns>
    public static ProjectAssetLocation? FindSource(this ProjectContext project, AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(identity);
        return AssetFileTypes.SourcesOf(identity)
            .Select(project.Find)
            .FirstOrDefault(location => location is not null && project.FileSystem.File.Exists(location.File.FullPath));
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
        if (holder is null)
        {
            return null;
        }

        if (holder.Library is { } library && file != holder.Root)
        {
            return library.VirtualPathOfDescriptor(file.GetPathBelow(holder.Root)) is { } name && AssetPath.TryParse(name, out var named)
                ? new(named, file, holder.Root, holder.Kind)
                : null;
        }

        return AssetPath.TryCreate(holder.Name, file.GetPathBelow(holder.Root), out var virtualPath)
            ? new(virtualPath, file, holder.Root, holder.Kind)
            : null;
    }

    /// <summary>
    /// Spells a file as a reference from a referrer: relative within the referrer's source mount, absolute from another
    /// source mount.
    /// </summary>
    /// <param name="project">The project.</param>
    /// <param name="referrer">The identity of the file that holds the reference.</param>
    /// <param name="file">The file.</param>
    /// <returns>The reference, or <see langword="null"/> when no bound source mount holds the file or its name is one a
    /// reference cannot spell.</returns>
    public static string? ToFileReference(this ProjectContext project, AssetPath referrer, FileSystemPath file)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(referrer);
        ArgumentNullException.ThrowIfNull(file);

        // Source mounts never overlap, so at most one holds the file.
        return project.SourceMounts.FirstOrDefault(mount => file != mount.Location && file.IsSameOrInside(mount.Location!)) is { } holder
            && AssetPath.TryCreate(holder.Name, file.GetPathBelow(holder.Location!), out var target)
            ? target.ToReference(referrer)
            : null;
    }

    /// <summary>Gets the file a mount path names in a bound source mount.</summary>
    /// <param name="project">The project.</param>
    /// <param name="mountPath">The mount path, such as <c>/Content/Ship/Ship.glb</c>.</param>
    /// <returns>The file, or <see langword="null"/> when the text is not a mount path, or its mount is not a bound
    /// source mount.</returns>
    public static FileSystemPath? SourceFileOf(this ProjectContext project, string mountPath)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(mountPath);
        if (mountPath.Length < 2 || mountPath[0] != '/')
        {
            return null;
        }

        var end = mountPath.IndexOf('/', 1);
        var name = end < 0 ? mountPath[1..] : mountPath[1..end];
        return project.SourceMounts.FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.Ordinal)) is { } mount
            ? end < 0 ? mount.Location : mount.Location!.Combine(mountPath[(end + 1)..])
            : null;
    }

    private static IEnumerable<Mount> Mounts(ProjectContext project)
        => project.Mounts
            .Where(static mount => mount.IsBound)
            .Select(static mount => new Mount(mount.Name, mount.Location!, mount.Kind == MountKind.Source ? ProjectMountKind.Authoring : ProjectMountKind.Cooked, mount.Library))
            .Append(new Mount(ProjectFilesMount, project.ProjectRoot, ProjectMountKind.ProjectFiles));

    private sealed record Mount(string Name, FileSystemPath Root, ProjectMountKind Kind, LibraryFacts? Library = null);
}
