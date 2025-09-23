// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the state of the content browser, including selected folders and the project root path.
/// This is a passive data holder that is updated by ViewModels.
/// </summary>
public sealed class ContentBrowserState(IProject currentProject) : INotifyPropertyChanged
{
    /// <summary>
    /// Occurs when a property changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

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
    /// This is a passive operation - no events are fired.
    /// </summary>
    /// <param name="folder">The folder to add.</param>
    public void AddSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        if (this.SelectedFolders.Add(pathRelativeToProjectRoot))
        {
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Removes a folder from the set of selected folders.
    /// This is a passive operation - no events are fired.
    /// </summary>
    /// <param name="folder">The folder to remove.</param>
    public void RemoveSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        if (this.SelectedFolders.Remove(pathRelativeToProjectRoot))
        {
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Sets the selected folders to contain only the specified folder.
    /// This is a passive operation - no events are fired.
    /// </summary>
    /// <param name="folder">The folder to select.</param>
    public void SetSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        this.SelectedFolders.Clear();
        this.SelectedFolders.Add(pathRelativeToProjectRoot);
        this.OnPropertyChanged(nameof(this.SelectedFolders));
    }

    /// <summary>
    /// Clears all selected folders.
    /// This is a passive operation - no events are fired.
    /// </summary>
    public void ClearSelection()
    {
        if (this.SelectedFolders.Count > 0)
        {
            this.SelectedFolders.Clear();
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Sets the selected folders to the specified collection of relative paths.
    /// </summary>
    /// <param name="relativePaths">The relative paths to set as selected.</param>
    public void SetSelectedFolders(IEnumerable<string> relativePaths)
    {
        this.SelectedFolders.Clear();
        foreach (var path in relativePaths)
        {
            this.SelectedFolders.Add(path);
        }
        this.OnPropertyChanged(nameof(this.SelectedFolders));
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

    private void OnPropertyChanged(string propertyName)
    {
        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
