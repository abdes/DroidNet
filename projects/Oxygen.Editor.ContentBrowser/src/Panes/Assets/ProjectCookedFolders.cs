// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Maps project-output folders using the saved virtual mount names.</summary>
internal static class ProjectCookedFolders
{
    /// <summary>Gets browser roots that expose the project's cooked directory.</summary>
    /// <param name="project">The active project, if any.</param>
    /// <returns>The configured virtual paths.</returns>
    public static AssetPath[] Roots(ProjectContext? project)
        => project?.AuthoringMounts.Where(static mount => string.Equals(mount.RelativePath.Replace('\\', '/').Trim('/'), ".cooked", StringComparison.OrdinalIgnoreCase))
            .Select(static mount => AssetPath.Create(mount.Name)).ToArray() ?? [];

    /// <summary>Removes a configured cooked mount prefix from a browser folder.</summary>
    /// <param name="selectedFolder">The browser location.</param>
    /// <param name="roots">The saved cooked mount paths.</param>
    /// <param name="runtimeFolder">The native virtual folder when matched; <see langword="null"/> for all of the output.</param>
    /// <returns>Whether this folder is a project-output projection.</returns>
    public static bool TryMap(AssetPath selectedFolder, IReadOnlyCollection<AssetPath> roots, out AssetPath? runtimeFolder)
    {
        ArgumentNullException.ThrowIfNull(selectedFolder);
        foreach (var root in roots.Where(selectedFolder.IsSameOrInside))
        {
            runtimeFolder = selectedFolder == root ? null : AssetPath.Parse("/" + selectedFolder.GetPathBelow(root));
            return true;
        }

        runtimeFolder = null;
        return false;
    }
}
