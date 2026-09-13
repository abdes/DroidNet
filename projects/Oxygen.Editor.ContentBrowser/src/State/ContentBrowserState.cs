// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Storage;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
/// Represents the state of the content browser, including selected folders and the project root path.
/// This is a passive data holder that is updated by ViewModels.
/// </summary>
public sealed partial class ContentBrowserState(IProjectContextService projectContextService) : INotifyPropertyChanged
{
    private readonly HashSet<string> selectedFolders = [with(StringComparer.Ordinal)];

    /// <summary>
    /// Occurs when a property changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the set of selected folders.
    /// </summary>
    public IReadOnlySet<string> SelectedFolders => this.selectedFolders;

    /// <summary>Gets the search and filter state shared by this browser's layouts.</summary>
    public AssetBrowserQuery Query { get; init; } = new();

    /// <summary>
    /// Gets the project root path.
    /// </summary>
    public string ProjectRootPath => projectContextService.ActiveProject?.ProjectRoot ?? string.Empty;

    /// <summary>Gets or sets the selected identity retained when switching asset layouts.</summary>
    internal Uri? SelectedAssetUri { get; set; }

    /// <summary>Gets or sets the layout allowed to change the shared asset selection.</summary>
    internal AssetsLayoutViewModel? ActiveAssetLayout { get; set; }

    /// <summary>Gets or sets initial shared discovery, so replacing a layout does not rescan the catalog.</summary>
    internal Task? AssetInitialization { get; set; }

    /// <summary>
    /// Adds a folder to the set of selected folders.
    /// Publishes the resulting scope as one state change.
    /// </summary>
    /// <param name="folder">The folder to add.</param>
    public void AddSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        if (this.selectedFolders.Add(pathRelativeToProjectRoot))
        {
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Removes a folder from the set of selected folders.
    /// Publishes the resulting scope as one state change.
    /// </summary>
    /// <param name="folder">The folder to remove.</param>
    public void RemoveSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        if (this.selectedFolders.Remove(pathRelativeToProjectRoot))
        {
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Sets the selected folders to contain only the specified folder.
    /// Publishes the resulting scope as one state change.
    /// </summary>
    /// <param name="folder">The folder to select.</param>
    public void SetSelectedFolder(IFolder folder)
    {
        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(this.ProjectRootPath);
        this.SetSelectedFolders([pathRelativeToProjectRoot]);
    }

    /// <summary>
    /// Clears all selected folders.
    /// Publishes an empty scope when selection changes.
    /// </summary>
    public void ClearSelection()
    {
        if (this.SelectedFolders.Count > 0)
        {
            this.selectedFolders.Clear();
            this.OnPropertyChanged(nameof(this.SelectedFolders));
        }
    }

    /// <summary>
    /// Sets the selected folders to the specified collection of relative paths.
    /// </summary>
    /// <param name="relativePaths">The relative paths to set as selected.</param>
    public void SetSelectedFolders(IEnumerable<string> relativePaths)
    {
        ArgumentNullException.ThrowIfNull(relativePaths);
        var replacement = relativePaths.Where(static path => !string.IsNullOrWhiteSpace(path)).ToHashSet(StringComparer.Ordinal);
        if (this.selectedFolders.SetEquals(replacement))
        {
            return;
        }

        this.selectedFolders.Clear();
        this.selectedFolders.UnionWith(replacement);
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
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
