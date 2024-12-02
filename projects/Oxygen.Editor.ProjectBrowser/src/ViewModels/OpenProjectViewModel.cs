// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Windows.Data;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
#pragma warning disable IDE0001 // Simplify Names
using IStorageItem = Oxygen.Editor.Storage.IStorageItem;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;
#pragma warning restore IDE0001 // Simplify Names

/// <summary>
/// ViewModel for the Open Project view in the Oxygen Editor's Project Browser.
/// </summary>
public partial class OpenProjectViewModel : ObservableObject, IRoutingAware
{
    private const SortDirection DefaultSortDirection = SortDirection.Ascending;

    private readonly ILogger logger;
    private readonly object fileListLock = new();
    private readonly IProjectBrowserService projectBrowser;
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

    private bool knownLocationLoaded;

    /// <summary>
    /// Initializes a new instance of the <see cref="OpenProjectViewModel"/> class.
    /// </summary>
    /// <param name="router">The router for navigating between views.</param>
    /// <param name="storageProvider">The storage provider for accessing storage items.</param>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="loggerFactory">
    /// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    /// cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public OpenProjectViewModel(
        IRouter router,
        NativeStorageProvider storageProvider,
        IProjectBrowserService projectBrowser,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<NewProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<NewProjectViewModel>();
        this.router = router;

        this.storageProvider = storageProvider;
        this.projectBrowser = projectBrowser;

        BindingOperations.EnableCollectionSynchronization(this.FileList, this.fileListLock);

        this.AdvancedFileList = new AdvancedCollectionView(this.FileList, isLiveShaping: true);
        this.AdvancedFileList.SortDescriptions.Add(new SortDescription(SortDirection.Descending, new ByFolderOrFileComparer()));
        this.AdvancedFileList.SortDescriptions.Add(new SortDescription(DefaultSortDirection, new ByNameComparer()));
    }

    public IList<KnownLocation> KnownLocations { get; private set; } = [];

    /// <summary>
    /// Gets the advanced collection view for the file list.
    /// </summary>
    public AdvancedCollectionView AdvancedFileList { get; init; }

    private ObservableCollection<IStorageItem> FileList { get; } = [];

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route) => await this.LoadKnownLocationsAsync().ConfigureAwait(true);

    /// <summary>
    /// Changes the current folder to the specified path.
    /// </summary>
    /// <param name="path">The path of the folder to change to.</param>
    [RelayCommand]
    private async Task ChangeFolderAsync(string path)
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

    /// <summary>
    /// Opens the project file at the specified location.
    /// </summary>
    /// <param name="location">The location of the project file to open.</param>
    /// <returns>A task that represents the asynchronous operation. The task result is <see langword="true"/> if the project was opened successfully; otherwise, <see langword="false"/>.</returns>
    [RelayCommand]
    private async Task<bool> OpenProjectFileAsync(string location)
    {
        var result = await this.projectBrowser.OpenProjectAsync(location).ConfigureAwait(true);
        if (!result)
        {
            return false;
        }

        await this.router.NavigateAsync("/we", new FullNavigation()).ConfigureAwait(true);
        return true;
    }

    /// <summary>
    /// Applies the specified filter to the file list.
    /// </summary>
    /// <param name="pattern">The filter pattern to apply.</param>
    [RelayCommand]
    private void ApplyFilter(string pattern)
        => this.AdvancedFileList.Filter = x => ((IStorageItem)x).Name.Contains(
            pattern,
            StringComparison.CurrentCultureIgnoreCase);

    /// <summary>
    /// Toggles the sorting of the file list by name.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByName()
    {
        var direction = this.byNameSortDescription != null
            ? this.byNameSortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending
            : DefaultSortDirection;
        this.byNameSortDescription = new SortDescription(direction, new ByNameComparer());

        this.AdvancedFileList.SortDescriptions.RemoveAt(1);
        this.byDateSortDescription = null;
        this.AdvancedFileList.SortDescriptions.Add(this.byNameSortDescription);
    }

    /// <summary>
    /// Toggles the sorting of the file list by date.
    /// </summary>
    [RelayCommand]
    private void ToggleSortByDate()
    {
        var direction = this.byDateSortDescription != null
            ? this.byDateSortDescription.Direction == SortDirection.Descending
                ? SortDirection.Ascending
                : SortDirection.Descending
            : DefaultSortDirection;
        this.byDateSortDescription = new SortDescription(direction, new ByAccessTimeComparer());

        this.AdvancedFileList.SortDescriptions.RemoveAt(1);
        this.byNameSortDescription = null;
        this.AdvancedFileList.SortDescriptions.Add(this.byDateSortDescription);
    }

    /// <summary>
    /// Navigates to the parent folder of the current folder.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CurrentFolderHasParent))]
    private void GoToParentFolder()
    {
        if (this.CurrentFolder is not INestedItem asNested)
        {
            return;
        }

        var parentPath = asNested.ParentPath;
        this.ChangeFolderCommand.Execute(parentPath);
    }

    /// <summary>
    /// Selects the specified known location.
    /// </summary>
    /// <param name="location">The known location to select.</param>
    [RelayCommand]
    private async Task SelectLocationAsync(KnownLocation location)
    {
        this.SelectedLocation = location;
        this.FileList.Clear();
        await foreach (var item in location.GetItems().ConfigureAwait(true))
        {
            this.FileList.Add(item);
        }

        this.CurrentFolder =
            location.Location.Length == 0
                ? null
                : await this.storageProvider.GetFolderFromPathAsync(location.Location).ConfigureAwait(true);
    }

    /// <summary>
    /// Called when the SelectedLocation property changes.
    /// </summary>
    /// <param name="value">The new value of the SelectedLocation property.</param>
    partial void OnSelectedLocationChanged(KnownLocation? value)
    {
        if (value != null)
        {
            SelectLocationCommand.Execute(value);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task LoadKnownLocationsAsync()
    {
        if (this.knownLocationLoaded)
        {
            return;
        }

        try
        {
            this.KnownLocations = await this.projectBrowser.GetKnownLocationsAsync().ConfigureAwait(true);
            this.knownLocationLoaded = true;
        }
        catch (Exception ex)
        {
            this.LogLoadingKnownLocationsError(ex);
        }
    }

    /// <summary>
    /// Determines whether the current folder has a parent folder.
    /// </summary>
    /// <returns><see langword="true"/> if the current folder has a parent folder; otherwise, <see langword="false"/>.</returns>
    private bool CurrentFolderHasParent()
        => this.CurrentFolder is INestedItem;

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload known locations during ViewModel activation")]
    private partial void LogLoadingKnownLocationsError(Exception ex);

    /// <summary>
    /// Compares storage items by whether they are folders or files.
    /// </summary>
    private sealed class ByFolderOrFileComparer : IComparer
    {
        /// <inheritdoc/>
        public int Compare(object? x, object? y) => x == y || (x is IFolder && y is IFolder) ? 0 : x is null or not IFolder ? -1 : 1;
    }

    /// <summary>
    /// Compares storage items by name.
    /// </summary>
    private sealed class ByNameComparer : IComparer
    {
        /// <inheritdoc/>
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

    /// <summary>
    /// Compares storage items by last access time.
    /// </summary>
    private sealed class ByAccessTimeComparer : IComparer
    {
        /// <inheritdoc/>
        public int Compare(object? x, object? y)
        {
            if (x is not IStorageItem && y is not IStorageItem)
            {
                return 0;
            }

            var itemX = x as IStorageItem;
            var itemY = y as IStorageItem;

            return DateTime.Compare(itemX!.LastAccessTime, itemY!.LastAccessTime);
        }
    }
}
