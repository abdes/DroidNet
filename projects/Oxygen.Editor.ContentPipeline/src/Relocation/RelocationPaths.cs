// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Maps between authoring virtual paths, physical files and asset identities. An identity is the
/// virtual path without the scheme and the <c>.json</c> authoring suffix, so a scene's
/// <c>asset:///Content/M/Red.omat.json</c> and a geometry's <c>/Content/M/Red.omat</c> are one asset.
/// </summary>
public static class RelocationPaths
{
    /// <summary>The importer's fixed output type folders at an authoring mount root.</summary>
    public static readonly IReadOnlyList<string> ImportTypeFolders = ["Materials", "Geometry", "Scenes"];

    private static readonly string[] CompoundExtensions =
        [".oscene.json", ".omat.json", ".ogeo.json", ".otex.json", ".opscene.json", ".ocshape.json", ".opmat.json", ".import.json"];

    /// <summary>Gets the asset identity of a virtual path: the path without its <c>.json</c> authoring suffix.</summary>
    /// <param name="virtualPath">A canonical virtual path such as <c>/Content/Materials/Red.omat.json</c>.</param>
    /// <returns>The identity, such as <c>/Content/Materials/Red.omat</c>.</returns>
    public static string ToIdentity(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        return virtualPath.EndsWith(".json", StringComparison.OrdinalIgnoreCase) && !virtualPath.EndsWith(".import.json", StringComparison.OrdinalIgnoreCase)
            && Path.GetExtension(virtualPath[..^5]).Length > 0
            ? virtualPath[..^5]
            : virtualPath;
    }

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
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The name a user edits when renaming.</returns>
    public static string GetDisplayName(string virtualPath)
    {
        var name = GetName(virtualPath);
        return name[..^GetExtension(name).Length];
    }

    /// <summary>Returns the path of a file or folder renamed in place, keeping its extension.</summary>
    /// <param name="virtualPath">The current virtual path.</param>
    /// <param name="newName">The new name, without the extension.</param>
    /// <param name="isFolder">Whether the path names a folder, which has no extension.</param>
    /// <returns>The renamed virtual path in the same folder.</returns>
    public static string Rename(string virtualPath, string newName, bool isFolder)
    {
        ArgumentNullException.ThrowIfNull(newName);
        var name = GetName(virtualPath);
        var extension = isFolder ? string.Empty : GetExtension(name);
        return Combine(GetParent(virtualPath), newName.Trim() + extension);
    }

    /// <summary>Validates one user-supplied file or folder name.</summary>
    /// <param name="name">The name, without path separators.</param>
    /// <returns>Why the name cannot be used, or null when it is valid.</returns>
    public static string? ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "Enter a name.";
        }

        if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains('/', StringComparison.Ordinal)
            || name.EndsWith('.') || name.EndsWith(' ') || name.StartsWith(' ') || name is "." or "..")
        {
            return "Use a name without path separators, reserved characters or a trailing dot or space.";
        }

        if (name.StartsWith('.'))
        {
            return "Names starting with a dot are reserved for derived content.";
        }

        var stem = name.Split('.')[0].ToUpperInvariant();
        return stem is "CON" or "PRN" or "AUX" or "NUL"
            || (stem.Length == 4 && stem[3] is >= '1' and <= '9' && (stem.StartsWith("COM", StringComparison.Ordinal) || stem.StartsWith("LPT", StringComparison.Ordinal)))
            ? "The name is reserved by the file system."
            : null;
    }

    /// <summary>Gets the last segment of a virtual path.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The file or folder name.</returns>
    public static string GetName(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        var trimmed = virtualPath.TrimEnd('/');
        return trimmed[(trimmed.LastIndexOf('/') + 1)..];
    }

    /// <summary>Gets the parent folder of a virtual path.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The parent folder, such as <c>/Content/Materials</c>.</returns>
    public static string GetParent(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        var trimmed = virtualPath.TrimEnd('/');
        var slash = trimmed.LastIndexOf('/');
        return slash <= 0 ? "/" : trimmed[..slash];
    }

    /// <summary>Joins a virtual folder and a child name.</summary>
    /// <param name="folder">The virtual folder.</param>
    /// <param name="name">The child name.</param>
    /// <returns>The child's virtual path.</returns>
    public static string Combine(string folder, string name)
    {
        ArgumentNullException.ThrowIfNull(folder);
        return folder.TrimEnd('/') + "/" + name.Trim('/');
    }

    /// <summary>Tests whether a virtual path equals a folder or lies inside it.</summary>
    /// <param name="path">The candidate path.</param>
    /// <param name="folder">The folder.</param>
    /// <returns>Whether <paramref name="path"/> is the folder or one of its descendants.</returns>
    public static bool IsSameOrInside(string path, string folder)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(folder);
        var root = folder.TrimEnd('/');
        return string.Equals(path, root, StringComparison.OrdinalIgnoreCase)
            || path.StartsWith(root + "/", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Gets the writable authoring mount that owns a virtual path.</summary>
    /// <param name="project">The project and its mounts.</param>
    /// <param name="virtualPath">A virtual path beginning with the mount name.</param>
    /// <returns>The mount, or null for derived, library or unknown mounts.</returns>
    public static ProjectMountPoint? FindWritableMount(ProjectContext project, string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(virtualPath);
        var trimmed = virtualPath.Trim('/');
        var name = trimmed.Split('/')[0];
        return GetWritableMounts(project).FirstOrDefault(mount => string.Equals(mount.Name, name, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>Gets the project's writable authoring mounts: project sources, never derived output.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The mounts whose files relocation may move and rewrite.</returns>
    public static IEnumerable<ProjectMountPoint> GetWritableMounts(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return project.AuthoringMounts.Where(mount =>
        {
            var relative = mount.RelativePath.Replace('\\', '/').Trim('/');
            if (relative.Length == 0 || relative.StartsWith('.') || Path.IsPathRooted(mount.RelativePath))
            {
                return false;
            }

            var full = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
            var root = Path.GetFullPath(project.ProjectRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            return full.StartsWith(root, StringComparison.OrdinalIgnoreCase);
        });
    }

    /// <summary>Resolves a virtual path in a writable authoring mount to its physical location.</summary>
    /// <param name="project">The project.</param>
    /// <param name="virtualPath">The virtual path.</param>
    /// <returns>The absolute path, or null when the path is not in a writable authoring mount.</returns>
    public static string? ToPhysical(ProjectContext project, string virtualPath)
    {
        if (FindWritableMount(project, virtualPath) is not { } mount)
        {
            return null;
        }

        var segments = virtualPath.Trim('/').Split('/');
        if (segments.Skip(1).Any(static segment => segment.Length == 0 || segment is "." or ".."))
        {
            return null;
        }

        var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
        return segments.Length == 1 ? root : Path.GetFullPath(Path.Combine([root, .. segments.Skip(1)]));
    }

    /// <summary>Maps a physical location inside a writable authoring mount to its virtual path.</summary>
    /// <param name="project">The project.</param>
    /// <param name="physicalPath">The absolute path.</param>
    /// <returns>The virtual path, or null outside writable authoring mounts.</returns>
    public static string? ToVirtual(ProjectContext project, string physicalPath)
    {
        ArgumentNullException.ThrowIfNull(physicalPath);
        var full = Path.GetFullPath(physicalPath).TrimEnd(Path.DirectorySeparatorChar);
        foreach (var mount in GetWritableMounts(project))
        {
            var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath)).TrimEnd(Path.DirectorySeparatorChar);
            if (string.Equals(full, root, StringComparison.OrdinalIgnoreCase))
            {
                return "/" + mount.Name;
            }

            if (full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                return "/" + mount.Name + "/" + full[(root.Length + 1)..].Replace('\\', '/');
            }
        }

        return null;
    }

    /// <summary>Gets the folder that holds the project's editor scenes, which are identified by name.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The absolute <c>Content/Scenes</c> folder.</returns>
    public static string GetEditorScenesFolder(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return Path.GetFullPath(Path.Combine(project.ProjectRoot, Constants.ContentFolderName, Constants.ScenesFolderName));
    }

    /// <summary>Tests whether a physical file is an editor scene: a scene file directly in <c>Content/Scenes</c>.</summary>
    /// <param name="project">The project.</param>
    /// <param name="physicalPath">The file.</param>
    /// <returns>Whether the file is one of the project's named scenes.</returns>
    public static bool IsEditorScene(ProjectContext project, string physicalPath)
    {
        ArgumentNullException.ThrowIfNull(physicalPath);
        return physicalPath.EndsWith(Constants.SceneFileExtension, StringComparison.OrdinalIgnoreCase)
            && string.Equals(Path.GetDirectoryName(Path.GetFullPath(physicalPath)), GetEditorScenesFolder(project), StringComparison.OrdinalIgnoreCase);
    }
}
