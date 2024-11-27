// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Collections;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media.Imaging;
using Oxygen.Editor.Projects;
using Windows.System;
using static Oxygen.Editor.ProjectBrowser.Controls.ProjectItemWithThumbnail;
using SortDescription = CommunityToolkit.WinUI.Collections.SortDescription;
using SortDirection = CommunityToolkit.WinUI.Collections.SortDirection;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A user control that displays a list of recent projects with sorting options by name or last used date.
/// </summary>
public sealed partial class RecentProjectsList
{
    /// <summary>
    /// A <see cref="IComparer"/> used when sorting recent projects by <see cref="IProjectInfo.LastUsedOn">last used date/time</see>.
    /// </summary>
    private static readonly ByLastUsedOnComparer SortByLastUsedOn = new();

    /// <summary>
    /// A <see cref="IComparer"/> used when sorting recent projects by <see cref="IProjectInfo.Name">name</see>.
    /// </summary>
    private static readonly ByNameComparer SortByName = new();

    /// <summary>
    /// The <see cref="RecentProjects"/> property gets a collection of <see cref="IProjectInfo"/>, but the control
    /// needs to display a more visually attractive item for each project, including its thumbnail. For that reason, we
    /// need to transform on the fly each item into a <see cref="ProjectItemWithThumbnail"/>.
    /// </summary>
    private DynamicObservableCollection<IProjectInfo, ProjectItemWithThumbnail>? projectItems;

    /// <summary>
    /// The <see cref="AdvancedCollectionView"/> provides sorting and filtering, and wraps the <see cref="projectItems"/>
    /// collection. This is the collection that should be used in UI binding and it should be bound with <see cref="BindingDirection.OneWay"/>,
    /// as it is recreated every time the <see cref="RecentProjects"/> property is set.
    /// </summary>
    private AdvancedCollectionView advancedProjectItems = [];

    /// <summary>
    /// When not <see langword="null"/>, specifies the current sorting mode of the recent projects list, either
    /// <see cref="SortByName"/> or <see cref="SortByLastUsedOn"/>.
    /// </summary>
    private SortDescription? sortDescription;

    /// <summary>
    /// Initializes a new instance of the <see cref="RecentProjectsList"/> class.
    /// </summary>
    public RecentProjectsList()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Occurs when a project item in the list is activated by the user.
    /// </summary>
    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user activates a project item in the list")]
    public event EventHandler<ProjectItemActivatedEventArgs>? ItemActivated;

    /// <summary>
    /// Gets or sets a value indicating whether the project item should be activated on double-tap.
    /// </summary>
    public bool ActivateOnDoubleTap { get; set; }

    /// <summary>
    /// Sets the collection of recent projects.
    /// </summary>
    public ObservableCollection<IProjectInfo> RecentProjects // TODO: This should become a dependency property
    {
        set
        {
            this.projectItems?.Dispose();
            this.projectItems = value.Transform(
                p =>
                {
                    var thumbUri = DefaultProjectThumbnail;
                    if (p is { Location: not null, Thumbnail: not null })
                    {
                        var thumbFilePath = Path.GetFullPath(Path.Combine(p.Location, p.Thumbnail));
                        if (File.Exists(thumbFilePath))
                        {
                            thumbUri = thumbFilePath;
                        }
                    }

                    var bitmap = new BitmapImage(new Uri(thumbUri));

                    return new ProjectItemWithThumbnail()
                    {
                        ProjectInfo = p,
                        Thumbnail = bitmap,
                    };
                });

            // Create the sortable collection and initialize its sorting criteria.
            this.advancedProjectItems = new AdvancedCollectionView(this.projectItems, isLiveShaping: true);

            this.sortDescription ??= new SortDescription(SortDirection.Descending, SortByLastUsedOn);
            this.advancedProjectItems.SortDescriptions.Add(this.sortDescription);
        }
    }

    /// <summary>
    /// Gets the default project thumbnail URI.
    /// </summary>
    private static string DefaultProjectThumbnail { get; }
        = $"ms-appx:///{typeof(ProjectInfo).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";

    /// <summary>
    /// Handles the double-tap event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        _ = args; // unused

        if (!this.ActivateOnDoubleTap)
        {
            return;
        }

        var listView = (ListView)sender;
        var selectedItem = (ProjectItemWithThumbnail)listView.SelectedItem;

        this.ItemActivated?.Invoke(this, new ProjectItemActivatedEventArgs(selectedItem.ProjectInfo));
    }

    /// <summary>
    /// Handles the click event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemClicked(object sender, ItemClickEventArgs args)
    {
        _ = sender; // unused

        if (this.ActivateOnDoubleTap)
        {
            return;
        }

        var item = (ProjectItemWithThumbnail)args.ClickedItem;
        this.ItemActivated?.Invoke(this, new ProjectItemActivatedEventArgs(item.ProjectInfo));
    }

    /// <summary>
    /// Toggles the sorting of the recent projects list by last used date.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByLastUsedOn()
    {
        var direction = this.sortDescription?.Comparer is ByLastUsedOnComparer
            ? this.sortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending
            : SortDirection.Descending;
        this.sortDescription = new SortDescription(direction, SortByLastUsedOn);

        this.advancedProjectItems.SortDescriptions.Clear();
        this.advancedProjectItems.SortDescriptions.Add(this.sortDescription);
    }

    /// <summary>
    /// Toggles the sorting of the recent projects list by name.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByName()
    {
        var direction = this.sortDescription?.Comparer is ByNameComparer
            ? this.sortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending
            : SortDirection.Ascending;
        this.sortDescription = new SortDescription(direction, SortByName);
        this.advancedProjectItems.SortDescriptions.Clear();
        this.advancedProjectItems.SortDescriptions.Add(this.sortDescription);
    }

    /// <summary>
    /// Handles the key down event on the list view.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="eventArgs">The event data.</param>
    private void OnListViewKeyDown(object sender, KeyRoutedEventArgs eventArgs)
    {
        if (eventArgs.Key is VirtualKey.Enter or VirtualKey.Space)
        {
            var listView = (ListView)sender;
            var selectedItem = (ProjectItemWithThumbnail)listView.SelectedItem;

            Debug.WriteLine("Item clicked");
            this.ItemActivated?.Invoke(this, new ProjectItemActivatedEventArgs(selectedItem.ProjectInfo));
        }
    }
}
