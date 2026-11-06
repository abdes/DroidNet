// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Maps the files a cook reads to their cook paths and back, through the project's resolved mount table.
/// </summary>
/// <remarks>
/// A cook path names a file as the ImportTool records it. A file in a bound source mount, inside or outside the project
/// tree, is named by its mount path (<c>/Content/Ship/Ship.glb</c>), as is a model import record, beside its model. Any
/// other file of the project, such as a generated scene descriptor, is named by its path relative to the project root.
/// Cook facts persist cook paths, so they stay valid when a mount is bound to another folder.
/// </remarks>
internal static class CookPaths
{
    /// <summary>The folder of a private input root that holds the captured files of source mounts, by mount name.</summary>
    /// <remarks>A dot-named folder is an output folder by rule, so no project file a cook reads has a path in it.</remarks>
    internal const string MountsFolder = ".mounts";

    /// <summary>Tests whether a cook path is a mount path.</summary>
    /// <param name="cookPath">The cook path.</param>
    /// <returns><see langword="true"/> for a mount path, <see langword="false"/> for a project-relative path.</returns>
    public static bool IsMountPath(string cookPath) => cookPath.StartsWith('/');

    /// <summary>Gets the cook path of a file.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="file">The file.</param>
    /// <returns>The file's cook path.</returns>
    /// <exception cref="InvalidDataException">The file lies outside every source mount and outside the project.</exception>
    public static string Of(ProjectContext project, FileSystemPath file)
        => TryOf(project, file)
            ?? throw new InvalidDataException($"'{file}' lies outside every source mount and outside the project; mount its folder before cooking it.");

    /// <summary>Gets the cook path of a file, if it has one.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="file">The file.</param>
    /// <returns>The file's cook path, or <see langword="null"/> outside every source mount and outside the project.</returns>
    public static string? TryOf(ProjectContext project, FileSystemPath file)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(file);

        // A source mount inside the project tree names its files before the project does.
        return MountPathOf(project, file)
            ?? (file != project.ProjectRoot && file.IsSameOrInside(project.ProjectRoot) ? file.GetPathBelow(project.ProjectRoot) : null);
    }

    /// <summary>
    /// Gets the mount path of a file in a bound source mount: the mount's name and the file's path below its folder,
    /// such as <c>/Content/Ship/Ship.glb</c>. Unlike a virtual path, the file's name may be any name a file may have.
    /// </summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="file">The file.</param>
    /// <returns>The mount path, or <see langword="null"/> when no bound source mount holds the file.</returns>
    public static string? MountPathOf(ProjectContext project, FileSystemPath file)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(file);

        // Source mounts never overlap, so at most one holds the file.
        return project.SourceMounts.FirstOrDefault(mount => file != mount.Location && file.IsSameOrInside(mount.Location!)) is { } holder
            ? "/" + holder.Name + "/" + file.GetPathBelow(holder.Location!)
            : null;
    }

    /// <summary>Gets the file a cook path names.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="cookPath">The cook path.</param>
    /// <returns>The file, or <see langword="null"/> when its mount is unknown, unbound or missing.</returns>
    public static FileSystemPath? TryFileOf(ProjectContext project, string cookPath)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentException.ThrowIfNullOrEmpty(cookPath);
        return IsMountPath(cookPath) ? project.SourceFileOf(cookPath) : project.ProjectRoot.Combine(cookPath);
    }

    /// <summary>Gets the file a cook path names, which the cook reads.</summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="cookPath">The cook path.</param>
    /// <returns>The file.</returns>
    /// <exception cref="InvalidDataException">The cook path names a mount that is not a bound source mount.</exception>
    public static FileSystemPath FileOf(ProjectContext project, string cookPath)
        => TryFileOf(project, cookPath)
            ?? throw new InvalidDataException($"'{cookPath}' names a mount that is not a bound source mount of this project.");

    /// <summary>
    /// Tests whether the file a cook path names exists under exactly that name, every part compared with its case below
    /// its mount's folder or the project root.
    /// </summary>
    /// <param name="project">The project and its mount table.</param>
    /// <param name="cookPath">The cook path.</param>
    /// <returns><see langword="true"/> when the file exists with the same case; <see langword="false"/> when it does
    /// not, or its mount is not bound.</returns>
    public static bool ExistsExactly(ProjectContext project, string cookPath)
    {
        ArgumentNullException.ThrowIfNull(project);
        if (!IsMountPath(cookPath))
        {
            return ExactSourcePath.Exists(project.FileSystem, project.ProjectRoot.FullPath, cookPath);
        }

        var file = project.SourceFileOf(cookPath);
        return file is not null
            && project.SourceMounts.FirstOrDefault(mount => file.IsSameOrInside(mount.Location!)) is { } holder
            && ExactSourcePath.Exists(project.FileSystem, holder.Location!.FullPath, file == holder.Location ? string.Empty : file.GetPathBelow(holder.Location!));
    }

    /// <summary>Gets the path below a private input root where a cook path's captured file is kept.</summary>
    /// <param name="cookPath">The cook path.</param>
    /// <returns>The path of the captured file, relative to the input root.</returns>
    public static string StoragePathOf(string cookPath) => IsMountPath(cookPath) ? MountsFolder + cookPath : cookPath;

    /// <summary>
    /// Tests whether persisted text is a cook path: a mount path, or a project-relative path outside
    /// <see cref="MountsFolder"/>, made of names a file may have and no <c>.</c> or <c>..</c>.
    /// </summary>
    /// <param name="cookPath">The text.</param>
    /// <returns>Whether the text is a cook path.</returns>
    public static bool IsValid(string? cookPath)
        => !string.IsNullOrWhiteSpace(cookPath) && (IsMountPath(cookPath)
            ? IsValidRelative(cookPath[1..])
            : IsValidRelative(cookPath) && !cookPath.StartsWith(MountsFolder + "/", StringComparison.OrdinalIgnoreCase));

    // A relative path below a folder: '/'-separated names, never '.', '..' or a name a file cannot have.
    private static bool IsValidRelative(string path)
        => path.Length != 0 && !Path.IsPathRooted(path) && !path.Contains('\\', StringComparison.Ordinal)
            && path.Split('/').All(static segment => segment is not ("" or "." or "..")
                && segment.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && !segment.EndsWith('.') && !segment.EndsWith(' '));
}
