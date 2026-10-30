// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// Canonical mount folders, as the engine's mount contract defines them: full paths with links resolved, so two
/// spellings of one folder compare equal. A folder that does not exist goes through its deepest existing parent.
/// </summary>
internal static class MountFolders
{
    private static readonly char[] Separators = ['/', '\\'];

    /// <summary>Gets the canonical form of a path.</summary>
    /// <param name="fileSystem">The file system.</param>
    /// <param name="path">An absolute path.</param>
    /// <returns>The canonical path.</returns>
    public static FileSystemPath Canonical(IFileSystem fileSystem, string path)
    {
        var full = fileSystem.Path.GetFullPath(path.Replace('\\', '/'));
        var root = fileSystem.Path.GetPathRoot(full) ?? string.Empty;
        var current = root;
        var segments = full[root.Length..].Split(Separators, StringSplitOptions.RemoveEmptyEntries);
        for (var index = 0; index < segments.Length; index++)
        {
            var next = fileSystem.Path.Combine(current, segments[index]);
            IFileSystemInfo? info = fileSystem.Directory.Exists(next) ? fileSystem.DirectoryInfo.New(next)
                : fileSystem.File.Exists(next) ? fileSystem.FileInfo.New(next)
                : null;
            if (info is null)
            {
                return FileSystemPath.From(fileSystem.Path.Combine([next, .. segments[(index + 1)..]]));
            }

            current = info.LinkTarget is null ? next : info.ResolveLinkTarget(returnFinalTarget: true)?.FullName ?? next;
        }

        return FileSystemPath.From(current);
    }

    /// <summary>Gets the segments of a canonical path below a canonical folder.</summary>
    /// <param name="path">The path.</param>
    /// <param name="folder">The folder.</param>
    /// <returns>The segments, empty for the folder itself, or <see langword="null"/> when the path is not in it.</returns>
    public static string[]? SegmentsBelow(FileSystemPath path, FileSystemPath folder)
    {
        var pathParts = path.FullPath.Split(Separators, StringSplitOptions.RemoveEmptyEntries);
        var folderParts = folder.FullPath.Split(Separators, StringSplitOptions.RemoveEmptyEntries);
        return pathParts.Length >= folderParts.Length
            && folderParts.Select((part, index) => string.Equals(part, pathParts[index], FileSystemPath.NameComparison)).All(static same => same)
            ? pathParts[folderParts.Length..]
            : null;
    }

    /// <summary>Gets whether a canonical path lies in an output folder: a dot-named folder below the project root.</summary>
    /// <param name="path">The path.</param>
    /// <param name="projectRoot">The canonical project root.</param>
    /// <returns><see langword="true"/> when a segment below the project root starts with a dot.</returns>
    public static bool IsInOutputFolder(FileSystemPath path, FileSystemPath projectRoot)
        => SegmentsBelow(path, projectRoot)?.Any(static segment => segment.StartsWith('.')) == true;
}
