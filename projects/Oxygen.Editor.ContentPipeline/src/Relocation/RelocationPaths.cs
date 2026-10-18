// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The relocation rules of the project's files: which mounts relocation may change, where a virtual path is on disk,
/// and how a rename keeps a file's type. Paths themselves, identities and comparisons are <see cref="AssetPath"/>'s.
/// </summary>
public static class RelocationPaths
{
    /// <summary>The importer's fixed output type folders at an authoring mount root.</summary>
    public static readonly IReadOnlyList<string> ImportTypeFolders = ["Materials", "Geometry", "Scenes"];

    private static readonly string[] CompoundExtensions =
        [AssetFileTypes.SceneDescriptor, AssetFileTypes.MaterialDescriptor, AssetFileTypes.GeometryDescriptor, AssetFileTypes.TextureDescriptor, AssetFileTypes.PhysicsSceneDescriptor, AssetFileTypes.CollisionShapeDescriptor, AssetFileTypes.PhysicsMaterialDescriptor, AssetFileTypes.ImportSettings];

    /// <summary>Gets a file name's extension, keeping Oxygen's compound authoring extensions whole.</summary>
    /// <param name="fileName">The file name.</param>
    /// <returns>The extension, such as <c>.omat.json</c> or <c>.png</c>; empty for a folder name.</returns>
    public static string GetExtension(string fileName)
    {
        ArgumentNullException.ThrowIfNull(fileName);
        return CompoundExtensions.FirstOrDefault(extension => fileName.EndsWith(extension, StringComparison.OrdinalIgnoreCase)
            && fileName.Length > extension.Length) is { } compound
            ? fileName[^compound.Length..]
            : Path.GetExtension(fileName);
    }

    /// <summary>Gets the display name of a file or folder: its name without the extension.</summary>
    /// <param name="fileName">The file or folder name.</param>
    /// <returns>The name a user edits when renaming.</returns>
    public static string GetDisplayName(string fileName)
    {
        ArgumentNullException.ThrowIfNull(fileName);
        return fileName[..^GetExtension(fileName).Length];
    }

    /// <summary>Returns the path of a file or folder renamed in place, keeping its extension.</summary>
    /// <param name="path">The current virtual path.</param>
    /// <param name="newName">The new name as typed, without the extension; it is trimmed and put in NFC.</param>
    /// <param name="isFolder">Whether the path names a folder, which has no extension.</param>
    /// <returns>The renamed virtual path in the same folder.</returns>
    /// <exception cref="ArgumentException">The new name is refused by the virtual path rules.</exception>
    public static AssetPath Rename(AssetPath path, string newName, bool isFolder)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(newName);
        return path.WithName(VirtualPath.NormalizeTypedName(newName) + (isFolder ? string.Empty : GetExtension(path.Name)));
    }

    /// <summary>Gets the writable authoring mount that owns a virtual path.</summary>
    /// <param name="project">The project and its mounts.</param>
    /// <param name="path">The virtual path.</param>
    /// <returns>The mount, or null for derived, library or unknown mounts.</returns>
    public static ProjectMountPoint? FindWritableMount(ProjectContext project, AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return GetWritableMounts(project).FirstOrDefault(mount => string.Equals(mount.Name, path.Mount, StringComparison.Ordinal));
    }

    /// <summary>Gets the project's writable authoring mounts: project sources, never derived output.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The mounts whose files relocation may move and rewrite.</returns>
    public static IEnumerable<ProjectMountPoint> GetWritableMounts(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var root = project.ProjectRoot;
        return project.AuthoringMounts.Where(mount =>
            mount.RelativePath.Replace('\\', '/').Trim('/').Length != 0
            && !ProjectOutputFolders.Contains(mount)
            && !Path.IsPathRooted(mount.RelativePath)
            && root.Combine(mount.RelativePath) is var folder && folder != root && folder.IsSameOrInside(root));
    }

    /// <summary>Resolves a virtual path in a writable authoring mount to its physical location.</summary>
    /// <param name="project">The project.</param>
    /// <param name="path">The virtual path.</param>
    /// <returns>The location on disk, or null when the path is not in a writable authoring mount.</returns>
    public static FileSystemPath? ToPhysical(ProjectContext project, AssetPath path)
        => FindWritableMount(project, path) is not null && project.Find(path) is { MountKind: ProjectMountKind.Authoring } location
            ? location.File
            : null;

    /// <summary>Maps a physical location inside a writable authoring mount to its virtual path.</summary>
    /// <param name="project">The project.</param>
    /// <param name="physicalPath">The absolute path.</param>
    /// <returns>The virtual path, or null outside writable authoring mounts.</returns>
    public static AssetPath? ToVirtual(ProjectContext project, FileSystemPath physicalPath)
    {
        ArgumentNullException.ThrowIfNull(physicalPath);
        return project.FindByFilePath(physicalPath) is { MountKind: ProjectMountKind.Authoring } location
            && FindWritableMount(project, location.Path) is not null
                ? location.Path
                : null;
    }
}
