// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the state of the content browser, including selected folders and the project root path.
/// </summary>
public sealed class ContentBrowserState(IProject currentProject)
{
    /// <summary>
    /// Gets the set of selected folders.
    /// </summary>
    public ISet<string> SelectedFolders { get; } = new HashSet<string>(StringComparer.Ordinal);

    /// <summary>
    /// Gets the project root path.
    /// </summary>
    public string ProjectRootPath { get; } = currentProject.ProjectInfo.Location ?? string.Empty;

    /// <summary>
    /// Adds a folder to the set of selected folders.
    /// </summary>
    /// <param name="folder">The folder to add.</param>
    public void AddSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        _ = this.SelectedFolders.Add(pathRelativeToProjectRoot);
    }

    /// <summary>
    /// Removes a folder from the set of selected folders.
    /// </summary>
    /// <param name="folder">The folder to remove.</param>
    public void RemoveSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        _ = this.SelectedFolders.Remove(pathRelativeToProjectRoot);
    }

    /// <summary>
    /// Checks if a folder is in the set of selected folders.
    /// </summary>
    /// <param name="folder">The folder to check.</param>
    /// <returns><see langword="true"/> if the folder is selected; otherwise, <see langword="false"/>.</returns>
    public bool ContainsSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        return this.SelectedFolders.Contains(pathRelativeToProjectRoot);
    }
}
