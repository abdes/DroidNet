// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Data;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI.Collections;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Storage;
using Oxygen.Storage.Native;
#pragma warning disable IDE0001 // Simplify Names
using IStorageItem = Oxygen.Storage.IStorageItem;
#pragma warning restore IDE0001 // Simplify Names

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

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
    private bool knownLocationLoaded;
    private bool isLoadingLocation;

    // Column widths for the file list view; kept as fields for internal storage and to
    // ensure fields are defined before constructors (style rules).
    private double nameColumnWidth = 420.0;
    private double dateColumnWidth = 160.0;

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
        this.logger = loggerFactory?.CreateLogger<OpenProjectViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<OpenProjectViewModel>();
        this.router = router;

        this.storageProvider = storageProvider;
        this.projectBrowser = projectBrowser;

        BindingOperations.EnableCollectionSynchronization(this.FileList, this.fileListLock);

        this.AdvancedFileList = new AdvancedCollectionView(this.FileList, isLiveShaping: true);
        this.AdvancedFileList.SortDescriptions.Add(new SortDescription(SortDirection.Descending, new ByFolderOrFileComparer()));
        this.AdvancedFileList.SortDescriptions.Add(new SortDescription(DefaultSortDirection, new ByNameComparer()));
    }

    /// <summary>
    /// Gets or sets the pixel width of the name column in the file list.
    /// </summary>
    public double NameColumnWidth
    {
        get => this.nameColumnWidth;
        set => this.SetProperty(ref this.nameColumnWidth, Math.Max(48.0, value));
    }

    /// <summary>
    /// Gets or sets the pixel width of the date column in the file list.
    /// </summary>
    public double DateColumnWidth
    {
        get => this.dateColumnWidth;
        set => this.SetProperty(ref this.dateColumnWidth, Math.Max(48.0, value));
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(GoToParentFolderCommand))]
    public partial IFolder? CurrentFolder { get; set; }

    [ObservableProperty]
    public partial string FilterText { get; set; } = string.Empty;

    [ObservableProperty]
    public partial bool IsActivating { get; set; }

    [ObservableProperty]
    public partial Dictionary<string, KnownLocation?> Locations { get; set; } = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CurrentFolder))]
    public partial KnownLocation? SelectedLocation { get; set; }

    /// <summary>
    /// Gets the list of known locations.
    /// </summary>
    public IList<KnownLocation> KnownLocations { get; private set; } = [];

    /// <summary>
    /// Gets the advanced collection view for the file list.
    /// </summary>
    public AdvancedCollectionView AdvancedFileList { get; init; }

    private ObservableCollection<IStorageItem> FileList { get; } = [];

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        await this.LoadKnownLocationsAsync().ConfigureAwait(true);
        await this.LoadColumnWidthsAsync().ConfigureAwait(true);
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
            settings.OpenViewColumnWidths = value;
            await this.projectBrowser.SaveSettingsAsync().ConfigureAwait(true);
            this.LogSavedColumnWidths();
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            // Ignore persistence errors but log them
            this.LogFailedToSaveColumnWidths(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    /// <summary>
    /// Resets the activation state to allow further project activation.
    /// </summary>
    internal void ResetActivationState() => this.IsActivating = false;

    private async Task LoadColumnWidthsAsync()
    {
        try
        {
            var settings = await this.projectBrowser.GetSettingsAsync().ConfigureAwait(true);
            var widths = settings.OpenViewColumnWidths;
            if (!string.IsNullOrEmpty(widths))
            {
                this.LogLoadedColumnWidths(widths);
                foreach (var part in widths.Split(';'))
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
            // Ignore persistence errors but log them
            this.LogFailedToLoadColumnWidths(ex);
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

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
        this.IsActivating = true;

        var result = await this.projectBrowser.OpenProjectAsync(location).ConfigureAwait(true);
        if (!result)
        {
            this.IsActivating = false;
            this.LogFailedToOpenProjectFile(location);
            return false;
        }

        await this.router.NavigateAsync("/we", new FullNavigation()
        {
            Target = new Target { Name = "wnd-we" },
            ReplaceTarget = true,
        }).ConfigureAwait(true);

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
        this.LogToggledSort();
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
        this.LogToggledSort();
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
        if (this.isLoadingLocation && this.SelectedLocation == location)
        {
            return;
        }

        try
        {
            this.isLoadingLocation = true;
            this.SelectedLocation = location;
            this.FileList.Clear();
            await foreach (var item in location.GetItems().ConfigureAwait(true))
            {
                this.FileList.Add(item);
            }

            this.CurrentFolder =
                string.IsNullOrEmpty(location.Location)
                    ? null
                    : await this.storageProvider.GetFolderFromPathAsync(location.Location).ConfigureAwait(true);
        }
        finally
        {
            this.isLoadingLocation = false;
        }
    }

    /// <summary>
    /// Called when the SelectedLocation property changes.
    /// </summary>
    /// <param name="value">The new value of the SelectedLocation property.</param>
    partial void OnSelectedLocationChanged(KnownLocation? value)
    {
        // Intentionally left empty. Logic moved to SelectLocationAsync to support reloading.
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task LoadKnownLocationsAsync()
    {
        if (this.knownLocationLoaded)
        {
            return;
        }

        this.LogLoadingKnownLocations();
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
