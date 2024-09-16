// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Controls;

using System;
using System.Collections;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Collections;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.Projects;
using Windows.System;
using static Oxygen.Editor.ProjectBrowser.Controls.ProjectItemWithThumbnail;
using SortDescription = CommunityToolkit.WinUI.Collections.SortDescription;
using SortDirection = CommunityToolkit.WinUI.Collections.SortDirection;

/// <summary>
/// A user control that can be used to show the recent projects list with sorting
/// by name or last used date.
/// </summary>
public sealed partial class RecentProjectsList
{
    /// <summary>
    /// A <see cref="IComparer" /> used when sorting recent projects by <see cref="IProjectInfo.Name">name</see>.
    /// </summary>
    private static readonly ByLastUsedOnComparer SortByLastUsedOn = new();

    /// <summary>
    /// A <see cref="IComparer" /> used when sorting recent projects by <see cref="IProjectInfo.LastUsedOn">last used date/time</see>.
    /// </summary>
    private static readonly ByNameComparer SortByName = new();

    /// <summary>
    /// The <see cref="RecentProjects" /> property gets a collection of <see cref="IProjectInfo" />, but the control
    /// needs to display a more visually attractive item for each project, including its thumbnail. For that reason, we
    /// need to transform on the fly each item into a <see cref="ProjectItemWithThumbnail" />.
    /// </summary>
    private DynamicObservableCollection<IProjectInfo, ProjectItemWithThumbnail>? projectItems;

    /// <summary>
    /// The <see cref="AdvancedCollectionView" /> gives us sorting and filtering, and is wrapping the <see cref="projectItems" />
    /// collection. This is the collection that should be used in UI binding and it should bound with <see cref="BindingDirection.OneWay" />,
    /// as it is recreated everytime the <see cref="RecentProjects" /> property is set.
    /// </summary>
    private AdvancedCollectionView advancedProjectItems = [];

    /// <summary>
    /// When not <see langword="null" />, specifies the current sorting mode of the recent projects list, either
    /// <see cref="SortByName">by name</see> or <see cref="SortByLastUsedOn">by last access date/time</see>.
    /// </summary>
    private SortDescription? sortDescription;

    public RecentProjectsList() => this.InitializeComponent();

    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user activates a project item in the list")]
    public event EventHandler<ItemActivatedEventArgs>? ItemActivated;

    public bool ActivateOnDoubleTap { get; set; }

    public ObservableCollection<IProjectInfo> RecentProjects
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

    private static string DefaultProjectThumbnail { get; }
        = $"ms-appx:///{typeof(LocalProjectsSource).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";

    private void OnProjectItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        _ = args; // unused

        if (!this.ActivateOnDoubleTap)
        {
            return;
        }

        var listView = (ListView)sender;
        var selectedItem = (ProjectItemWithThumbnail)listView.SelectedItem;

        this.ItemActivated?.Invoke(this, new ItemActivatedEventArgs(selectedItem.ProjectInfo));
    }

    private void OnProjectItemClicked(object sender, ItemClickEventArgs args)
    {
        _ = sender; // unused

        if (this.ActivateOnDoubleTap)
        {
            return;
        }

        var item = (ProjectItemWithThumbnail)args.ClickedItem;
        this.ItemActivated?.Invoke(this, new ItemActivatedEventArgs(item.ProjectInfo));
    }

    [RelayCommand]
    private void ToggleSortByLastUsedOn()
    {
        SortDirection direction;
        if (this.sortDescription?.Comparer is ByLastUsedOnComparer)
        {
            direction = this.sortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending;
        }
        else
        {
            direction = SortDirection.Descending;
        }

        this.sortDescription = new SortDescription(direction, SortByLastUsedOn);

        this.advancedProjectItems.SortDescriptions.Clear();
        this.advancedProjectItems.SortDescriptions.Add(this.sortDescription);
    }

    [RelayCommand]
    private void ToggleSortByName()
    {
        SortDirection direction;
        if (this.sortDescription?.Comparer is ByNameComparer)
        {
            direction = this.sortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending;
        }
        else
        {
            direction = SortDirection.Ascending;
        }

        this.sortDescription = new SortDescription(direction, SortByName);
        this.advancedProjectItems.SortDescriptions.Clear();
        this.advancedProjectItems.SortDescriptions.Add(this.sortDescription);
    }

    private void OnListViewKeyDown(object sender, KeyRoutedEventArgs eventArgs)
    {
        if (eventArgs.Key is VirtualKey.Enter or VirtualKey.Space)
        {
            var listView = (ListView)sender;
            var selectedItem = (ProjectItemWithThumbnail)listView.SelectedItem;

            Debug.WriteLine("Item clicked");
            this.ItemActivated?.Invoke(this, new ItemActivatedEventArgs(selectedItem.ProjectInfo));
        }
    }

    public class ItemActivatedEventArgs(IProjectInfo projectInfo) : EventArgs
    {
        public IProjectInfo ProjectInfo => projectInfo;
    }
}

internal sealed class ProjectItemWithThumbnail
{
    public required IProjectInfo ProjectInfo { get; init; }

    public required ImageSource Thumbnail { get; init; }

    public sealed class ByNameComparer : IComparer
    {
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return string.CompareOrdinal(item1.ProjectInfo.Name, item2.ProjectInfo.Name);
        }
    }

    public sealed class ByLastUsedOnComparer : IComparer
    {
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return DateTime.Compare(item1.ProjectInfo.LastUsedOn, item2.ProjectInfo.LastUsedOn);
        }
    }
}
