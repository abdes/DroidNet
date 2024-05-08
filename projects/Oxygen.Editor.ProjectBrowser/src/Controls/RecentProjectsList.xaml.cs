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
using DynamicData;
using DynamicData.Binding;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Oxygen.Editor.ProjectBrowser.Projects;
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
    private IDisposable? oldListSubscription;

    private ReadOnlyObservableCollection<ProjectItemWithThumbnail> projectItems = new(
        new ObservableCollection<ProjectItemWithThumbnail>());

    public RecentProjectsList()
    {
        this.InitializeComponent();

        this.AdvancedProjectItems = new AdvancedCollectionView(this.projectItems, true);
    }

    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user clicks a project item in the list")]
    public event EventHandler<IProjectInfo>? ItemClick;

    public ObservableCollection<IProjectInfo> RecentProjects
    {
        set
        {
            this.oldListSubscription?.Dispose();
            var x = value.ToObservableChangeSet()
                .Transform(
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
                    })
                .Bind(out this.projectItems);

            this.AdvancedProjectItems = new AdvancedCollectionView(this.projectItems, true);

            this.oldListSubscription = x.Subscribe();
        }
    }

    internal AdvancedCollectionView AdvancedProjectItems { get; private set; }

    internal ReadOnlyObservableCollection<ProjectItemWithThumbnail> ProjectItems => this.projectItems;

    private static string DefaultProjectThumbnail { get; }
        = $"ms-appx:///{typeof(LocalProjectsSource).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";

    private static IComparer SortByLastUsedOn() => new ProjectItemWithThumbnail.SortByLastUsedOn();

    private static IComparer SortByName() => new ProjectItemWithThumbnail.SortByName();

    private void OnProjectItemClick(object sender, ItemClickEventArgs e)
    {
        Debug.WriteLine("Item clicked");
        this.ItemClick?.Invoke(this, ((ProjectItemWithThumbnail)e.ClickedItem).ProjectInfo);
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
