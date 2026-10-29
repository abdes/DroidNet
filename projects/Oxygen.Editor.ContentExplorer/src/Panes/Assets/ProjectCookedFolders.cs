// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.Assets;

/// <summary>Maps project-output folders using the saved virtual mount names.</summary>
internal static class ProjectCookedFolders
{
    /// <summary>Gets the browser root of the project's Cooked node, when the Content Explorer shows it.</summary>
    /// <param name="project">The active project, if any.</param>
    /// <returns>The node's virtual path, or none.</returns>
    public static AssetPath[] Roots(ProjectContext? project)
        => project?.OutputFolderViews.Where(static view => string.Equals(view.Folder, ProjectOutputFolders.Cooked, StringComparison.Ordinal))
            .Select(static view => AssetPath.Create(view.Name)).ToArray() ?? [];

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
