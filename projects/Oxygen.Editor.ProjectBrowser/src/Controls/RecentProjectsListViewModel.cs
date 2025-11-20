// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Collections;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// ViewModel for the recent projects list control.
/// Manages the transformation of source IProjectInfo collection into displayable ProjectItemWithThumbnail items
/// using DynamicObservableCollection.Transform() to automatically track changes.
/// </summary>
internal partial class RecentProjectsListViewModel : ObservableObject
{
    private readonly IProjectBrowserService projectBrowser;
    private readonly ILogger logger;
    private readonly ProjectItemWithThumbnail.ByNameComparer byNameComparer = new();
    private readonly ProjectItemWithThumbnail.ByLastUsedOnComparer byLastUsedComparer = new();
    private SortDescription? currentSortDescription;
    private DynamicObservableCollection<IProjectInfo, ProjectItemWithThumbnail>? transformedItems;

    // Column widths
    private double nameColumnWidth = 400.0;
    private double dateColumnWidth = 150.0;

    /// <summary>
    /// Initializes a new instance of the <see cref="RecentProjectsListViewModel"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="defaultThumbnailUri">The default thumbnail URI to use when no thumbnail is available.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public RecentProjectsListViewModel(IProjectBrowserService projectBrowser, string defaultThumbnailUri, ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<RecentProjectsListViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<RecentProjectsListViewModel>();

        this.projectBrowser = projectBrowser;
        this.DefaultThumbnailUri = defaultThumbnailUri;
        this.ItemsView = new AdvancedCollectionView(new ObservableCollection<ProjectItemWithThumbnail>(), isLiveShaping: true);
        this.currentSortDescription ??= new SortDescription(SortDirection.Descending, this.byLastUsedComparer);
        this.ItemsView.SortDescriptions.Add(this.currentSortDescription);

        this.LoadColumnWidthsAsync();

        this.LogInitialized(this.ItemsView.Source?.GetHashCode());
    }

    /// <summary>
    /// Event invoked when a project item is activated in the ViewModel.
    /// </summary>
    public event EventHandler<RecentProjectActivatedEventArgs>? ItemActivated;

    [ObservableProperty]
    public partial ProjectItemWithThumbnail? SelectedProject { get; set; }

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
    [NotifyPropertyChangedFor(nameof(IsNotBusy))]
    [ObservableProperty]
    public partial bool IsBusy { get; set; }

    /// <summary>
    /// Gets a value indicating whether no project activation is currently in progress.
    /// </summary>
    public bool IsNotBusy => !this.IsBusy;

    /// <summary>
    /// Gets or sets the pixel width of the name column.
    /// </summary>
    public double NameColumnWidth
    {
        get => this.nameColumnWidth;
        set => this.SetProperty(ref this.nameColumnWidth, Math.Max(48.0, value));
    }

    /// <summary>
    /// Gets or sets the pixel width of the date column.
    /// </summary>
    public double DateColumnWidth
    {
        get => this.dateColumnWidth;
        set => this.SetProperty(ref this.dateColumnWidth, Math.Max(48.0, value));
    }

    /// <summary>
    /// Persists the column widths to the local settings.
    /// </summary>
    public async void SaveColumnWidths()
    {
        try
        {
            var value = $"Name:{this.NameColumnWidth.ToString(CultureInfo.InvariantCulture)};LastModifiedDate:{this.DateColumnWidth.ToString(CultureInfo.InvariantCulture)}";
            var settings = await this.projectBrowser.GetSettingsAsync().ConfigureAwait(true);
            settings.HomeViewColumnWidths = value;
            await this.projectBrowser.SaveSettingsAsync().ConfigureAwait(true);
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogFailedToSave(ex.Message);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

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
    public void ResetActivationState() => this.IsBusy = false;

    private async void LoadColumnWidthsAsync()
    {
        try
        {
            var settings = await this.projectBrowser.GetSettingsAsync().ConfigureAwait(true);
            var widths = settings.HomeViewColumnWidths;
            if (!string.IsNullOrEmpty(widths))
            {
                var parts = widths.Split(';');
                foreach (var part in parts)
                {
                    var kvp = part.Split(':');
                    if (kvp.Length == 2 && double.TryParse(kvp[1], CultureInfo.InvariantCulture, out var width))
                    {
                        if (string.Equals(kvp[0], "Name", StringComparison.Ordinal))
                        {
                            this.NameColumnWidth = width;
                        }
                        else if (string.Equals(kvp[0], "LastModifiedDate", StringComparison.Ordinal))
                        {
                            this.DateColumnWidth = width;
                        }
                    }
                }
            }
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            this.LogFailedToLoad(ex.Message);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    /// <summary>
    /// Command to activate a project item. Sets IsBusy to true while executing.
    /// </summary>
    [RelayCommand]
    private void ActivateProject(ProjectItemWithThumbnail? item)
    {
        if (item is null)
        {
            return;
        }

        this.LogActivatingProject(item.ProjectInfo.Name);
        this.IsBusy = true;
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
        this.LogTogglingSortByName(this.currentSortDescription.Comparer.GetType().Name, this.currentSortDescription.Direction.ToString(), this.ItemsView.Count);
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
        this.LogTogglingSortByLastUsedOn(this.currentSortDescription.Comparer.GetType().Name, this.currentSortDescription.Direction.ToString(), this.ItemsView.Count);
    }
}
