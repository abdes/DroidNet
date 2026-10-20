// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Finds the files beside a source that cook to the same asset: <c>move.lua</c> and <c>move.luau</c> both cook to
/// <c>move.oscript</c>, and <c>Red.omat.json</c> and <c>red.omat.json</c> name one file on a case-insensitive file
/// system. Each asset has one source, so no authoring operation may create such a pair, and the cook refuses one it
/// finds.
/// </summary>
/// <remarks>
/// Only sources that cook into a descriptor of their own can share one; a container's products are named under its
/// own path, which no other source has.
/// </remarks>
public static class SourceCollisions
{
    /// <summary>Finds the other files beside a source that cook to the same asset.</summary>
    /// <param name="project">The project that holds the source.</param>
    /// <param name="source">The source, or the location a source would be written to.</param>
    /// <param name="ignored">A file that does not count, such as the source of a rename.</param>
    /// <returns>The colliding files, sorted by name; empty when the source has none or cooks into no descriptor.</returns>
    public static IReadOnlyList<FileSystemPath> Find(ProjectContext project, FileSystemPath source, FileSystemPath? ignored = null)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        var files = project.FileSystem;
        if (CookedName(source.Name) is not { } cooked || source.Parent is not { } folder || !files.Directory.Exists(folder.FullPath))
        {
            return [];
        }

        return [.. files.Directory.EnumerateFiles(folder.FullPath)
            .Select(FileSystemPath.From)
            .Where(other => !string.Equals(other.Name, source.Name, StringComparison.Ordinal) && other != ignored
                && string.Equals(CookedName(other.Name), cooked, StringComparison.OrdinalIgnoreCase))
            .OrderBy(static other => other.Name, StringComparer.Ordinal)];
    }

    // The cooked descriptor a source cooks into; identities differing only in case are one file in a cooked root.
    private static string? CookedName(string name)
        => AssetFileTypes.Find(name)?.Category is AssetFileCategory.Descriptor or AssetFileCategory.Script ? AssetFileTypes.ToCookedPath(name) : null;
}
