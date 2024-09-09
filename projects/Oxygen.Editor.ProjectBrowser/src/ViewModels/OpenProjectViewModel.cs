// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using System.Collections;
using System.Collections.ObjectModel;
using System.Windows.Data;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
#pragma warning disable IDE0001 // Simplify Names
using IStorageItem = Oxygen.Editor.Storage.IStorageItem;

#pragma warning restore IDE0001 // Simplify Names

/// <summary>ViewModel for the page used to open an existing project in the project browser.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class OpenProjectViewModel : ObservableObject
{
    private const SortDirection DefaultSortDirection = SortDirection.Ascending;

    private readonly object fileListLock = new();
    private readonly IProjectBrowserService projectBrowserService;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly IRouter router;

#pragma warning disable CA1859 // Use concrete types when possible for improved performance
    private readonly IStorageProvider storageProvider;
#pragma warning restore CA1859 // Use concrete types when possible for improved performance

    private SortDescription? byDateSortDescription;
    private SortDescription? byNameSortDescription;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(GoToParentFolderCommand))]
    private IFolder? currentFolder;

    [ObservableProperty]
    private string filterText = string.Empty;

    [ObservableProperty]
    private Dictionary<string, KnownLocation?> locations = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CurrentFolder))]
    private KnownLocation? selectedLocation;

    public OpenProjectViewModel(
        IRouter router,
        NativeStorageProvider storageProvider,
        IProjectBrowserService projectBrowserService)
    {
        this.dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        this.router = router;

        this.storageProvider = storageProvider;
        this.projectBrowserService = projectBrowserService;

        BindingOperations.EnableCollectionSynchronization(this.FileList, this.fileListLock);

        this.AdvancedFileList = new AdvancedCollectionView(this.FileList, isLiveShaping: true);

        this.AdvancedFileList.SortDescriptions.Add(
            new SortDescription(SortDirection.Descending, new ByFolderOrFileComparer()));
        this.AdvancedFileList.SortDescriptions.Add(new SortDescription(DefaultSortDirection, new ByNameComparer()));
    }

    public AdvancedCollectionView AdvancedFileList { get; init; }

    private ObservableCollection<IStorageItem> FileList { get; } = [];

    public async void SelectLocation(KnownLocation location)
    {
        this.SelectedLocation = location;
        this.FileList.Clear();
        await foreach (var item in location.GetItems().ConfigureAwait(true))
        {
            this.FileList.Add(item);
        }

        if (location.Location != string.Empty)
        {
            this.CurrentFolder
                = await this.storageProvider.GetFolderFromPathAsync(location.Location).ConfigureAwait(true);
        }
    }

    public async Task Initialize()
    {
        this.OnPropertyChanging(nameof(this.Locations));

        foreach (var location in await this.projectBrowserService.GetKnownLocationsAsync().ConfigureAwait(true))
        {
            this.Locations[location.Key.ToString()] = location;
        }

        this.OnPropertyChanged(nameof(this.Locations));

        var knownLocation = this.Locations.First()
            .Value;
        if (knownLocation != null)
        {
            this.SelectLocation(knownLocation);
        }
    }

    public async void ChangeFolder(string path)
    {
        this.CurrentFolder = await this.storageProvider.GetFolderFromPathAsync(path).ConfigureAwait(true);
        this.FileList.Clear();
        await Task.Run(
                async () =>
                {
                    await foreach (var item in this.CurrentFolder.GetItemsAsync().ConfigureAwait(true))
                    {
                        lock (this.fileListLock)
                        {
                            this.FileList.Add(item);
                        }
                    }
                })
            .ConfigureAwait(true);
    }

    public async Task<bool> OpenProjectFile(string location)
    {
        var success = await this.projectBrowserService.LoadProjectInfoAsync(location!).ConfigureAwait(false);
        if (success)
        {
            success = this.dispatcherQueue.TryEnqueue(() => this.router.Navigate("/we", new FullNavigation()));
        }

        return success;
    }

    public void ApplyFilter(string pattern)
        => this.AdvancedFileList.Filter = x => ((IStorageItem)x).Name.Contains(
            pattern,
            StringComparison.CurrentCultureIgnoreCase);

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
            direction = DefaultSortDirection;
        }

        this.byNameSortDescription = new SortDescription(direction, new ByNameComparer());

        this.AdvancedFileList.SortDescriptions.RemoveAt(1);
        this.byDateSortDescription = null;
        this.AdvancedFileList.SortDescriptions.Add(this.byNameSortDescription);
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
            direction = DefaultSortDirection;
        }

        this.byDateSortDescription = new SortDescription(direction, new ByAccessTimeComparer());

        this.AdvancedFileList.SortDescriptions.RemoveAt(1);
        this.byNameSortDescription = null;
        this.AdvancedFileList.SortDescriptions.Add(this.byDateSortDescription);
    }

    [RelayCommand(CanExecute = nameof(CurrentFolderHasParent))]
    private void GoToParentFolder()
    {
        if (this.CurrentFolder is not INestedItem asNested)
        {
            return;
        }

        var parentPath = asNested.ParentPath;
        this.ChangeFolder(parentPath);
    }

    private bool CurrentFolderHasParent()
        => this.CurrentFolder is INestedItem;

    private sealed class ByFolderOrFileComparer : IComparer
    {
        public int Compare(object? x, object? y)
        {
            if (x == y || (x is IFolder && y is IFolder))
            {
                return 0;
            }

            if (x is null or not IFolder)
            {
                return -1;
            }

            return 1;
        }
    }

    private sealed class ByNameComparer : IComparer
    {
        public int Compare(object? x, object? y)
        {
            if (x is not IStorageItem && y is not IStorageItem)
            {
                return 0;
            }

            var itemX = x as IStorageItem;
            var itemY = y as IStorageItem;

            return string.CompareOrdinal(itemX!.Name, itemY!.Name);
        }
    }

    private sealed class ByAccessTimeComparer : IComparer
    {
        public int Compare(object? x, object? y)
        {
            if (x is not IStorageItem && y is not IStorageItem)
            {
                return 0;
            }

            var itemX = x as IStorageItem;
            var itemY = y as IStorageItem;

            return DateTime.Compare(itemX!.LastModifiedTime, itemY!.LastModifiedTime);
        }
    }
}
