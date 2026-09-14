// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Maps project-output folders using the saved virtual mount names.</summary>
internal static class ProjectCookedFolders
{
    /// <summary>Gets browser roots that expose the project's cooked directory.</summary>
    /// <param name="project">The active project, if any.</param>
    /// <returns>The configured virtual paths.</returns>
    public static string[] Roots(ProjectContext? project)
        => project?.AuthoringMounts.Where(static mount => string.Equals(mount.RelativePath.Replace('\\', '/').Trim('/'), ".cooked", StringComparison.OrdinalIgnoreCase))
            .Select(static mount => "/" + mount.Name).ToArray() ?? [];

    /// <summary>Removes a configured cooked mount prefix from a browser folder.</summary>
    /// <param name="selectedFolder">The browser location.</param>
    /// <param name="roots">The saved cooked mount paths.</param>
    /// <param name="runtimePath">The native virtual folder, when matched.</param>
    /// <returns>Whether this folder is a project-output projection.</returns>
    public static bool TryMap(string selectedFolder, IReadOnlyCollection<string> roots, out string runtimePath)
    {
        var normalized = "/" + selectedFolder.Replace('\\', '/').Trim('/');
        foreach (var root in roots)
        {
            if (normalized.Equals(root, StringComparison.OrdinalIgnoreCase)
                || normalized.StartsWith(root + "/", StringComparison.OrdinalIgnoreCase))
            {
                runtimePath = normalized.Length == root.Length ? "/" : normalized[root.Length..];
                return true;
            }
        }

        runtimePath = string.Empty;
        return false;
    }
}
