// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// A base ViewModel for the assets view layout.
/// </summary>
public abstract partial class AssetsLayoutViewModel(
    IContentBrowserAssetProvider assetProvider,
    IProjectContextService projectContextService,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext,
    IBuiltinCatalogDiscovery builtins) : ObservableObject, IRoutingAware, IDisposable
{
    private readonly IContentBrowserAssetProvider assetProvider = assetProvider;
    private readonly IProjectContextService projectContextService = projectContextService;
    private readonly ContentBrowserState contentBrowserState = contentBrowserState;
    private readonly HostingContext hostingContext = hostingContext;
    private readonly IBuiltinCatalogDiscovery builtins = builtins;

    private IDisposable? subscription;
    private IDisposable? builtinChanges;
    private bool disposed;
    private Task? initialization;
    private TaskCompletionSource? initialSnapshotCompletion;
    private IReadOnlyList<ContentBrowserAssetItem> latestItems = [];
    private ContentBrowserAssetItem? selectedAsset;
    private bool isLoading = true;
    private bool isReplacingRows;
    private bool hasSnapshot;
    private Uri? pendingReveal;
    private string[] projectCookedFolders = [];

    /// <summary>
    /// Occurs when an item in the assets view is invoked.
    /// </summary>
    public event EventHandler<AssetsViewItemInvokedEventArgs>? ItemInvoked;

    /// <summary>Occurs when an explicit navigation asks the view to reveal its selected asset.</summary>
    public event EventHandler? SelectionRevealRequested;

    /// <summary>
    /// Gets the collection of content browser asset rows.
    /// </summary>
    public ObservableCollection<AssetBrowserRow> Assets { get; } = [];

    /// <summary>Gets the shared session query used by every layout.</summary>
    public AssetBrowserQuery Query => this.contentBrowserState.Query;

    /// <summary>Gets the empty-state visibility after initial discovery.</summary>
    public Visibility EmptyVisibility => !this.isLoading && this.Assets.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the initial discovery indicator visibility.</summary>
    public Visibility LoadingVisibility => this.isLoading ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the empty-state heading for this scope and query.</summary>
    public string EmptyTitle => this.Query.IsActive ? "No matching assets" : this.IsCookedScope() ? "No cooked assets here" : "This folder is empty";

    /// <summary>Gets the next action for an empty scope.</summary>
    public string EmptyMessage => this.Query.IsActive ? "Try another search or clear the filters."
        : this.IsCookedScope() ? "Cook authored content to see its published output here."
        : "Create a material or import content using the toolbar above.";

    /// <summary>Gets the reset action visibility for filtered emptiness.</summary>
    public Visibility ClearQueryVisibility => this.Query.IsActive ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the source-navigation action visibility in an empty derived view.</summary>
    public Visibility BrowseSourceVisibility => !this.Query.IsActive && this.IsCookedScope() ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the catalog availability notice when the current scope includes engine assets.</summary>
    public string? BuiltinCatalogNotice => this.IncludesEngineChoices() ? this.builtins.Snapshot.Notice : null;

    /// <summary>Gets a value indicating whether the current scope needs a catalog notice.</summary>
    public bool HasBuiltinCatalogNotice => !string.IsNullOrEmpty(this.BuiltinCatalogNotice);

    /// <summary>
    /// Gets or sets the currently selected asset row, if any.
    /// </summary>
    public ContentBrowserAssetItem? SelectedAsset
    {
        get => this.selectedAsset;
        set
        {
            if (this.disposed || this.isReplacingRows)
            {
                return;
            }

            if (value is null)
            {
                _ = this.SetProperty(ref this.selectedAsset, newValue: null);
            }
            else if (this.Assets.FirstOrDefault(row => AssetIdentityGrouping.Represents(row.Item, value.IdentityUri)) is { } current)
            {
                _ = this.SetProperty(ref this.selectedAsset, current.Item);
            }

            if (ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this))
            {
                this.contentBrowserState.SelectedAssetUri = this.selectedAsset?.IdentityUri;
            }

            if (this.pendingReveal != this.selectedAsset?.IdentityUri)
            {
                this.pendingReveal = null;
            }

            this.OnPropertyChanged(nameof(this.SelectedRow));
        }
    }

    /// <summary>Gets the current visual row so programmatic selection is reflected in either layout.</summary>
    public AssetBrowserRow? SelectedRow => this.selectedAsset is { } selected
        ? this.Assets.FirstOrDefault(row => AssetIdentityGrouping.Represents(row.Item, selected.IdentityUri)) : null;

    /// <summary>Requests one reveal for explicit navigation, including a view that has not loaded yet.</summary>
    public void RevealSelection()
    {
        this.pendingReveal = this.SelectedAsset?.IdentityUri;
        this.SelectionRevealRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>
    /// Forces a refresh of <see cref="Assets"/> by re-querying the provider.
    /// </summary>
    /// <returns>Completion of the shared refresh.</returns>
    public Task RefreshAsync() => this.assetProvider.RefreshAsync(AssetBrowserFilter.Default);

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        try
        {
            await (this.initialization ??= this.InitializeAsync()).ConfigureAwait(true);
            ObjectDisposedException.ThrowIf(this.disposed, this);
            if (!ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this))
            {
                this.pendingReveal = null;
            }

            this.contentBrowserState.ActiveAssetLayout = this;
            if (this.hasSnapshot)
            {
                this.RestoreSelection(this.contentBrowserState.SelectedAssetUri);
            }
        }
        catch
        {
            this.initialization = null;
            throw;
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>Normalizes folder selections without letting a root selection broaden a concrete scope.</summary>
    /// <param name="folders">The selected virtual folder paths.</param>
    /// <returns>The distinct normalized folders.</returns>
    internal static IReadOnlyList<string> NormalizeSelectedFolders(IEnumerable<string>? folders)
    {
        if (folders is null)
        {
            return [];
        }

        var normalized = folders
            .Where(static folder => !string.IsNullOrWhiteSpace(folder))
            .Select(NormalizeFolderPath)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        if (normalized.Count > 1)
        {
            _ = normalized.RemoveAll(static folder => string.Equals(folder, "/", StringComparison.Ordinal));
        }

        return normalized;
    }

    /// <summary>Checks authored and cooked projections against the current browser scope.</summary>
    /// <param name="displayPath">The row's display location.</param>
    /// <param name="identityAbsolutePath">The authored virtual path.</param>
    /// <param name="selectedFolders">The normalized selected folders.</param>
    /// <param name="hasActiveProject">Whether the browser owns a project.</param>
    /// <param name="cookedAbsolutePath">The known cooked virtual path.</param>
    /// <param name="hasCookedProjection">Whether a cooked projection exists.</param>
    /// <param name="cookedFolders">The saved virtual mount paths exposing project output.</param>
    /// <returns>Whether the asset belongs to this scope.</returns>
    internal static bool IsInSelectedFolders(
        string displayPath,
        string identityAbsolutePath,
        IReadOnlyCollection<string> selectedFolders,
        bool hasActiveProject,
        string? cookedAbsolutePath = null,
        bool hasCookedProjection = false,
        IReadOnlyCollection<string>? cookedFolders = null)
    {
        if (selectedFolders.Count == 0
            || selectedFolders.Contains(".", StringComparer.OrdinalIgnoreCase)
            || selectedFolders.Contains("/", StringComparer.OrdinalIgnoreCase))
        {
            return true;
        }

        if (!hasActiveProject)
        {
            return true;
        }

        foreach (var selected in selectedFolders)
        {
            if (ProjectCookedFolders.TryMap(selected, cookedFolders ?? [], out var cookedSelection))
            {
                if (hasCookedProjection
                    && (string.Equals(cookedSelection, "/", StringComparison.OrdinalIgnoreCase)
                        || (cookedAbsolutePath is not null && IsSameOrChildPath(cookedAbsolutePath, cookedSelection))))
                {
                    return true;
                }

                continue;
            }

            if (IsSameOrChildPath(displayPath, selected)
                || IsSameOrChildPath(identityAbsolutePath, selected))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>Consumes a still-current reveal request when its view is ready.</summary>
    /// <returns>The selected row to reveal, or null after superseding selection/navigation.</returns>
    internal AssetBrowserRow? TakeSelectionToReveal()
    {
        var row = this.SelectedRow;
        if (this.disposed || !ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this)
            || row is null || this.pendingReveal is null || !AssetIdentityGrouping.Represents(row.Item, this.pendingReveal))
        {
            return null;
        }

        this.pendingReveal = null;
        return row;
    }

    /// <summary>
    /// Releases the managed subscriptions.
    /// </summary>
    /// <param name="disposing">Whether managed resources should be released.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.disposed)
        {
            if (disposing)
            {
                _ = this.initialSnapshotCompletion?.TrySetCanceled();
                this.subscription?.Dispose();
                this.builtinChanges?.Dispose();
                this.contentBrowserState.PropertyChanged -= this.ContentBrowserState_PropertyChanged;
                this.Query.Changed -= this.OnQueryChanged;
                if (ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this))
                {
                    this.contentBrowserState.ActiveAssetLayout = null;
                }
            }

            this.disposed = true;
        }
    }

    /// <summary>
    /// Invokes the <see cref="ItemInvoked"/> event.
    /// </summary>
    /// <param name="item">The asset row that was invoked.</param>
    protected void OnItemInvoked(ContentBrowserAssetItem item)
    {
        if (!this.disposed && ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this)
            && this.Assets.FirstOrDefault(row => AssetIdentityGrouping.Represents(row.Item, item.IdentityUri)) is { } current)
        {
            this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(current.Item));
        }
    }

    private static bool HasCookedProjection(ContentBrowserAssetItem asset)
        => asset.PrimaryState is AssetState.Cooked
           || (asset.BuiltinOriginUri is not null && asset.CookedUri is not null)
           || asset.DerivedState is AssetState.Cooked or AssetState.Stale
           || (asset.PrimaryState is AssetState.Broken && asset.CookedUri == asset.IdentityUri);

    private static string NormalizeFolderPath(string value)
    {
        var normalized = value.Replace('\\', '/').Trim('/');
        return string.IsNullOrEmpty(normalized) || string.Equals(normalized, ".", StringComparison.Ordinal) ? "/" : "/" + normalized;
    }

    private static bool IsSameOrChildPath(string candidate, string folder)
    {
        var normalizedCandidate = NormalizeFolderPath(candidate);
        return normalizedCandidate.Equals(folder, StringComparison.OrdinalIgnoreCase)
               || normalizedCandidate.StartsWith(folder + "/", StringComparison.OrdinalIgnoreCase);
    }

    private async Task InitializeAsync()
    {
        var firstSnapshot = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.initialSnapshotCompletion = firstSnapshot;
        this.subscription?.Dispose();
        this.builtinChanges?.Dispose();
        this.contentBrowserState.PropertyChanged -= this.ContentBrowserState_PropertyChanged;
        this.Query.Changed -= this.OnQueryChanged;
        this.builtinChanges = Observable.FromEventPattern(
                handler => this.builtins.Changed += handler,
                handler => this.builtins.Changed -= handler)
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(_ => this.NotifyBuiltinStatus());
        this.subscription = this.assetProvider.Items
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(
                items =>
                {
                    this.ReplaceItems(items);
                    _ = firstSnapshot.TrySetResult();
                },
                exception => firstSnapshot.TrySetException(exception),
                () => firstSnapshot.TrySetResult());
        this.contentBrowserState.PropertyChanged += this.ContentBrowserState_PropertyChanged;
        this.Query.Changed += this.OnQueryChanged;
        var refresh = this.contentBrowserState.AssetInitialization ??= this.RefreshAsync();
        try
        {
            await refresh.ConfigureAwait(true);
            await firstSnapshot.Task.ConfigureAwait(true);
        }
        catch
        {
            if (ReferenceEquals(this.contentBrowserState.AssetInitialization, refresh))
            {
                this.contentBrowserState.AssetInitialization = null;
            }

            throw;
        }

        this.isLoading = false;
        this.NotifyEmptyState();
    }

    private void ContentBrowserState_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            this.ReplaceItems(this.latestItems);
            this.NotifyBuiltinStatus();
        }
    }

    private void ReplaceItems(IReadOnlyList<ContentBrowserAssetItem> items)
    {
        this.projectCookedFolders = ProjectCookedFolders.Roots(this.projectContextService.ActiveProject);
        if (this.disposed)
        {
            return;
        }

        this.latestItems = items;
        this.hasSnapshot = true;
        var selectedUri = ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this)
            ? this.contentBrowserState.SelectedAssetUri : this.SelectedAsset?.IdentityUri;
        var existing = this.Assets.ToDictionary(static row => row.Item.IdentityUri.AbsoluteUri, StringComparer.OrdinalIgnoreCase);
        var scopedItems = this.projectContextService.ActiveProject is { } project
            ? CookedLibraryProjection.ForFolders(items, project, NormalizeSelectedFolders(this.contentBrowserState.SelectedFolders)) : items;
        var visible = AssetIdentityGrouping.GroupBuiltins(scopedItems.Where(this.IsInSelectedFolders)).Where(this.Query.Matches).ToArray();
        this.isReplacingRows = true;
        try
        {
            for (var index = 0; index < visible.Length; index++)
            {
                var item = visible[index];
                if (existing.TryGetValue(item.IdentityUri.AbsoluteUri, out var row))
                {
                    row.Update(item);
                    if (!ReferenceEquals(this.Assets[index], row))
                    {
                        this.Assets.Move(this.Assets.IndexOf(row), index);
                    }
                }
                else
                {
                    this.Assets.Insert(index, new(item));
                }
            }

            while (this.Assets.Count > visible.Length)
            {
                this.Assets.RemoveAt(this.Assets.Count - 1);
            }
        }
        finally
        {
            this.isReplacingRows = false;
        }

        this.RestoreSelection(selectedUri);
        this.NotifyEmptyState();
    }

    private void RestoreSelection(Uri? identity)
        => this.SelectedAsset = identity is null ? null : this.Assets.FirstOrDefault(row => AssetIdentityGrouping.Represents(row.Item, identity))?.Item;

    private void OnQueryChanged(object? sender, EventArgs args) => this.ReplaceItems(this.latestItems);

    [RelayCommand]
    private void BrowseSource()
    {
        var sourceFolders = NormalizeSelectedFolders(this.contentBrowserState.SelectedFolders)
            .Select(folder => ProjectCookedFolders.TryMap(folder, this.projectCookedFolders, out var source) ? source : folder)
            .ToArray();
        if (sourceFolders.Contains("/", StringComparer.Ordinal))
        {
            var mounts = this.projectContextService.ActiveProject?.AuthoringMounts
                .Where(static mount => !ProjectExplorer.ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(mount))
                .Select(static mount => "/" + mount.Name).ToArray() ?? [];
            this.contentBrowserState.SetSelectedFolders(mounts.Length == 0 ? ["/"] : mounts);
        }
        else
        {
            this.contentBrowserState.SetSelectedFolders(sourceFolders);
        }
    }

    private bool IsCookedScope()
        => NormalizeSelectedFolders(this.contentBrowserState.SelectedFolders).Any(folder => ProjectCookedFolders.TryMap(folder, this.projectCookedFolders, out _));

    private void NotifyEmptyState()
    {
        if (!this.disposed)
        {
            this.OnPropertyChanged(nameof(this.EmptyVisibility));
            this.OnPropertyChanged(nameof(this.LoadingVisibility));
            this.OnPropertyChanged(nameof(this.EmptyTitle));
            this.OnPropertyChanged(nameof(this.EmptyMessage));
            this.OnPropertyChanged(nameof(this.ClearQueryVisibility));
            this.OnPropertyChanged(nameof(this.BrowseSourceVisibility));
        }
    }

    private bool IsInSelectedFolders(ContentBrowserAssetItem asset)
    {
        var selectedFolders = NormalizeSelectedFolders(this.contentBrowserState.SelectedFolders);
        return IsInSelectedFolders(
            asset.DisplayPath,
            asset.IdentityUri.AbsolutePath,
            selectedFolders,
            this.projectContextService.ActiveProject is not null,
            asset.CookedUri?.AbsolutePath,
            HasCookedProjection(asset),
            this.projectCookedFolders);
    }

    [RelayCommand]
    private async Task RetryBuiltinCatalogAsync()
    {
        _ = await this.builtins.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
        await this.RefreshAsync().ConfigureAwait(true);
    }

    private bool IncludesEngineChoices()
    {
        var folders = NormalizeSelectedFolders(this.contentBrowserState.SelectedFolders);
        return folders.Count == 0 || folders.Any(static folder => folder is "/" or "."
            || folder.Equals("/Engine", StringComparison.OrdinalIgnoreCase)
            || folder.StartsWith("/Engine/", StringComparison.OrdinalIgnoreCase));
    }

    private void NotifyBuiltinStatus()
    {
        if (!this.disposed)
        {
            this.OnPropertyChanged(nameof(this.BuiltinCatalogNotice));
            this.OnPropertyChanged(nameof(this.HasBuiltinCatalogNotice));
        }
    }
}
