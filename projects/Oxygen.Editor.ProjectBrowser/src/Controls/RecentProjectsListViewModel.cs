// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Collections;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// ViewModel for the recent projects list control.
/// Manages the transformation of source IProjectInfo collection into displayable ProjectItemWithThumbnail items
/// using DynamicObservableCollection.Transform() to automatically track changes.
/// </summary>
internal partial class RecentProjectsListViewModel : ObservableObject
{
    private readonly ProjectItemWithThumbnail.ByNameComparer byNameComparer = new();
    private readonly ProjectItemWithThumbnail.ByLastUsedOnComparer byLastUsedComparer = new();
    private SortDescription? currentSortDescription;
    private DynamicObservableCollection<IProjectInfo, ProjectItemWithThumbnail>? transformedItems;

    /// <summary>
    /// Initializes a new instance of the <see cref="RecentProjectsListViewModel"/> class.
    /// </summary>
    /// <param name="defaultThumbnailUri">The default thumbnail URI to use when no thumbnail is available.</param>
    public RecentProjectsListViewModel(string defaultThumbnailUri)
    {
        this.DefaultThumbnailUri = defaultThumbnailUri;
        this.ItemsView = new AdvancedCollectionView(new ObservableCollection<ProjectItemWithThumbnail>(), isLiveShaping: true);
        this.currentSortDescription ??= new SortDescription(SortDirection.Descending, this.byLastUsedComparer);
        this.ItemsView.SortDescriptions.Add(this.currentSortDescription);
        Debug.WriteLine($"[RecentProjectsListViewModel] Initialized: ItemsView.Source.Hash={this.ItemsView.Source?.GetHashCode()}");
    }

    /// <summary>
    /// Event invoked when a project item is activated in the ViewModel.
    /// </summary>
    public event EventHandler<RecentProjectActivatedEventArgs>? ItemActivated;

    /// <summary>
    /// Gets or sets the default thumbnail URI.
    /// </summary>
    [ObservableProperty]
    public partial string DefaultThumbnailUri { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the collection view for the items.
    /// </summary>
    [ObservableProperty]
    public partial AdvancedCollectionView ItemsView { get; set; } = null!;

    /// <summary>
    /// Gets a value indicating whether a project activation is in progress.
    /// </summary>
    [ObservableProperty]
    public partial bool IsActivating { get; set; }

    /// <summary>
    /// Sets the source collection of recent projects and transforms them for display.
    /// The transformation uses DynamicObservableCollection.Transform() to automatically track changes.
    /// AdvancedCollectionView automatically reflects these changes with sorting/filtering.
    /// </summary>
    /// <param name="sourceProjects">The source collection of recent projects to display.</param>
    public void SetRecentProjects(ObservableCollection<IProjectInfo>? sourceProjects)
    {
        this.transformedItems?.Dispose();

        if (sourceProjects is null || sourceProjects.Count == 0)
        {
            // Set empty collection
            this.ItemsView.Source = new ObservableCollection<ProjectItemWithThumbnail>();
            return;
        }

        // Create transformed collection that auto-tracks source changes.
        // Transform() handles all collection change notifications automatically.
        this.transformedItems = sourceProjects.Transform(
            p => ProjectItemWithThumbnail.Create(p, this.DefaultThumbnailUri));

        // Set the transformed collection as the source for AdvancedCollectionView.
        // AdvancedCollectionView will automatically apply sorting and live shaping
        // as items are added/removed/modified in the transformed collection.
        this.ItemsView.Source = this.transformedItems;
    }

    /// <summary>
    /// Re-enables activation after command execution completes (success or failure).
    /// </summary>
    internal void ResetActivationState()
    {
        this.IsActivating = false;
    }

    /// <summary>
    /// Command to activate a project item. Sets IsActivating to true while executing.
    /// </summary>
    [RelayCommand]
    private void ActivateProject(ProjectItemWithThumbnail? item)
    {
        if (item is null)
        {
            return;
        }

        Debug.WriteLine($"[RecentProjectsListViewModel] ActivateProject: Project={item.ProjectInfo.Name}");
        this.IsActivating = true;
        this.ItemActivated?.Invoke(this, new RecentProjectActivatedEventArgs(item));
    }

    /// <summary>
    /// Toggles the sorting of the recent projects list by name.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByName()
    {
        var direction = this.currentSortDescription?.Comparer is ProjectItemWithThumbnail.ByNameComparer
            ? (this.currentSortDescription.Direction == SortDirection.Descending ? SortDirection.Ascending : SortDirection.Descending)
            : SortDirection.Ascending;

        this.currentSortDescription = new SortDescription(direction, this.byNameComparer);
        this.ItemsView.SortDescriptions.Clear();
        this.ItemsView.SortDescriptions.Add(this.currentSortDescription);
        Debug.WriteLine($"[RecentProjectsListViewModel] ToggleSortByName: Now sort comparer={this.currentSortDescription.Comparer.GetType().Name}, Direction={this.currentSortDescription.Direction}, ItemsView.Count={this.ItemsView.Count}");
    }

    /// <summary>
    /// Toggles the sorting of the recent projects list by last used date.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByLastUsedOn()
    {
        var direction = this.currentSortDescription?.Comparer is ProjectItemWithThumbnail.ByLastUsedOnComparer
            ? (this.currentSortDescription.Direction == SortDirection.Descending ? SortDirection.Ascending : SortDirection.Descending)
            : SortDirection.Descending;

        this.currentSortDescription = new SortDescription(direction, this.byLastUsedComparer);
        this.ItemsView.SortDescriptions.Clear();
        this.ItemsView.SortDescriptions.Add(this.currentSortDescription);
        Debug.WriteLine($"[RecentProjectsListViewModel] ToggleSortByLastUsedOn: Now sort comparer={this.currentSortDescription.Comparer.GetType().Name}, Direction={this.currentSortDescription.Direction}, ItemsView.Count={this.ItemsView.Count}");
    }
}
