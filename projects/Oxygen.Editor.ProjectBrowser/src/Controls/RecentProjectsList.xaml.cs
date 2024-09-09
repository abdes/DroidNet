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
using Microsoft.UI.Xaml;
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
    private SortDescription? byDateSortDescription;
    private SortDescription? byNameSortDescription;
    private DynamicObservableCollection<IProjectInfo, ProjectItemWithThumbnail>? dynamicProjectItems;

    public RecentProjectsList()
    {
        this.InitializeComponent();

        this.AdvancedProjectItems = new AdvancedCollectionView(this.ProjectItems, isLiveShaping: true);
    }

    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user clicks a project item in the list")]
    public event EventHandler<ItemActivatedEventArgs>? ItemActivated;

    public ObservableCollection<IProjectInfo> RecentProjects
    {
        set
        {
            this.dynamicProjectItems?.Dispose();
            this.dynamicProjectItems = value.Transform(
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

            this.ProjectItems = new ReadOnlyObservableCollection<ProjectItemWithThumbnail>(this.dynamicProjectItems);
            this.AdvancedProjectItems = new AdvancedCollectionView(this.ProjectItems, isLiveShaping: true);
        }
    }

    internal AdvancedCollectionView AdvancedProjectItems { get; private set; }

    internal ReadOnlyObservableCollection<ProjectItemWithThumbnail> ProjectItems { get; private set; } = new([]);

    private static string DefaultProjectThumbnail { get; }
        = $"ms-appx:///{typeof(LocalProjectsSource).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";

    private static SortByLastUsedOn SortByLastUsedOn() => new();

    private static SortByName SortByName() => new();

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.dynamicProjectItems?.Dispose();
        this.dynamicProjectItems = null;
    }

    private void OnProjectItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs eventArgs)
    {
        _ = eventArgs;

        var listView = (ListView)sender;
        var selectedItem = (ProjectItemWithThumbnail)listView.SelectedItem;

        Debug.WriteLine("Item clicked");
        this.ItemActivated?.Invoke(this, new ItemActivatedEventArgs(selectedItem.ProjectInfo));
    }

    [RelayCommand]
    private void ToggleSortByDate()
    {
        SortDirection direction;
        if (this.byDateSortDescription != null)
        {
            direction = this.byDateSortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending;
        }
        else
        {
            direction = SortDirection.Descending;
        }

        this.byDateSortDescription = new SortDescription(direction, SortByLastUsedOn());
        this.AdvancedProjectItems.SortDescriptions.Clear();
        this.byNameSortDescription = null;
        this.AdvancedProjectItems.SortDescriptions.Add(this.byDateSortDescription);
    }

    [RelayCommand]
    private void ToggleSortByName()
    {
        SortDirection direction;
        if (this.byNameSortDescription != null)
        {
            direction = this.byNameSortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending;
        }
        else
        {
            direction = SortDirection.Ascending;
        }

        this.byNameSortDescription = new SortDescription(direction, SortByName());
        this.AdvancedProjectItems.SortDescriptions.Clear();
        this.byDateSortDescription = null;
        this.AdvancedProjectItems.SortDescriptions.Add(this.byNameSortDescription);
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

    public sealed class SortByName : IComparer
    {
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return string.CompareOrdinal(item1.ProjectInfo.Name, item2.ProjectInfo.Name);
        }
    }

    public sealed class SortByLastUsedOn : IComparer
    {
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return DateTime.Compare(item1.ProjectInfo.LastUsedOn, item2.ProjectInfo.LastUsedOn);
        }
    }
}
