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
using Oxygen.Editor.Assets;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// A base ViewModel for the assets view layout.
/// </summary>
public abstract partial class AssetsLayoutViewModel(
    IAssetFeed assetProvider,
    IProjectContextService projectContextService,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext,
    IScripts? scripts = null) : ObservableObject, IRoutingAware, IDisposable
{
    private readonly IAssetFeed assetProvider = assetProvider;
    private readonly IProjectContextService projectContextService = projectContextService;
    private readonly ContentBrowserState contentBrowserState = contentBrowserState;
    private readonly HostingContext hostingContext = hostingContext;

    private IDisposable? subscription;
    private IDisposable? scriptChanges;
    private bool disposed;
    private Task? initialization;
    private TaskCompletionSource? initialSnapshotCompletion;
    private IReadOnlyList<AssetEntry> latestItems = [];
    private IReadOnlyList<AssetEntry> selectedAssets = [];
    private AssetEntry? selectedAsset;
    private int scopeCount;
    private bool isLoading = true;
    private bool isReplacingRows;
    private bool hasSnapshot;
    private AssetPath? pendingReveal;
    private AssetPath[] projectCookedFolders = [];

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

    /// <summary>Gets the shared result order, tile size and view.</summary>
    public AssetBrowserPresentation Presentation => this.contentBrowserState.Presentation;

    /// <summary>
    /// Gets or sets the active asset. Setting it selects that asset alone; null clears the selection.
    /// </summary>
    public AssetEntry? SelectedAsset
    {
        get => this.selectedAsset;
        set => this.SetSelection(value is null ? [] : [value]);
    }

    /// <summary>Gets the selected assets in selection order; the last is <see cref="SelectedAsset"/>.</summary>
    public IReadOnlyList<AssetEntry> SelectedAssets => this.selectedAssets;

    /// <summary>Gets the current visual row so programmatic selection is reflected in every layout.</summary>
    public AssetBrowserRow? SelectedRow => this.selectedAsset is { } selected ? this.FindRow(selected.Identity) : null;

    /// <summary>Gets the visual rows of every selected asset.</summary>
    public IReadOnlyList<AssetBrowserRow> SelectedRows => [.. this.selectedAssets.Select(asset => this.FindRow(asset.Identity)).OfType<AssetBrowserRow>()];

    private bool IsActiveLayout => !this.disposed && ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this);

    /// <summary>Replaces the selection with visible assets; hidden or unknown assets are dropped.</summary>
    /// <param name="assets">The assets to select in selection order; the last becomes active.</param>
    public void SetSelection(IEnumerable<AssetEntry> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        if (this.disposed || this.isReplacingRows)
        {
            return;
        }

        // A repeated asset keeps its latest position, so the most recent pick stays active.
        var selection = assets
            .Select(asset => this.FindRow(asset.Identity)?.Item)
            .OfType<AssetEntry>()
            .Reverse()
            .DistinctBy(static asset => asset.Identity)
            .Reverse()
            .ToArray();
        var changed = !this.selectedAssets.SequenceEqual(selection);
        this.selectedAssets = selection;
        _ = this.SetProperty(ref this.selectedAsset, selection.Length == 0 ? null : selection[^1], nameof(this.SelectedAsset));
        this.PublishSelection();
        if (this.pendingReveal != this.selectedAsset?.Identity)
        {
            this.pendingReveal = null;
        }

        if (changed)
        {
            this.OnPropertyChanged(nameof(this.SelectedAssets));
            this.OnPropertyChanged(nameof(this.SelectedRows));
        }

        this.OnPropertyChanged(nameof(this.SelectedRow));
    }

    /// <summary>Requests one reveal for explicit navigation, including a view that has not loaded yet.</summary>
    public void RevealSelection()
    {
        this.pendingReveal = this.SelectedAsset?.Identity;
        this.SelectionRevealRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Opens an asset the way a double-click does.</summary>
    /// <param name="item">The asset to open.</param>
    public void Invoke(AssetEntry item)
    {
        ArgumentNullException.ThrowIfNull(item);
        this.OnItemInvoked(item);
    }

    /// <summary>
    /// Forces a refresh of <see cref="Assets"/> by re-querying the provider.
    /// </summary>
    /// <returns>Completion of the shared refresh.</returns>
    public Task RefreshAsync() => this.assetProvider.RefreshAsync();

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
                this.ReplaceItems(this.latestItems);
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

    /// <summary>Checks authored and cooked projections against the current browser scope.</summary>
    /// <param name="displayPath">The row's display location.</param>
    /// <param name="identity">The asset's identity.</param>
    /// <param name="selectedFolders">The selected folders; none selected is the whole project.</param>
    /// <param name="hasActiveProject">Whether the browser owns a project.</param>
    /// <param name="hasCookedProjection">Whether a cooked projection exists.</param>
    /// <param name="cookedFolders">The mounts exposing project output.</param>
    /// <returns>Whether the asset belongs to this scope.</returns>
    internal static bool IsInSelectedFolders(
        string displayPath,
        AssetPath identity,
        IReadOnlyCollection<AssetPath> selectedFolders,
        bool hasActiveProject,
        bool hasCookedProjection = false,
        IReadOnlyCollection<AssetPath>? cookedFolders = null)
    {
        if (selectedFolders.Count == 0 || !hasActiveProject)
        {
            return true;
        }

        // A library's copy shows under the library's mount, where its display path names it.
        var shown = AssetPath.TryParse(displayPath, out var location) ? location : identity;
        foreach (var selected in selectedFolders)
        {
            if (ProjectCookedFolders.TryMap(selected, cookedFolders ?? [], out var cookedSelection))
            {
                if (hasCookedProjection && (cookedSelection is null || identity.IsSameOrInside(cookedSelection)))
                {
                    return true;
                }

                continue;
            }

            if (shown.IsSameOrInside(selected) || identity.IsSameOrInside(selected))
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
            || row is null || this.pendingReveal is null || row.Item.Identity != this.pendingReveal)
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
                this.scriptChanges?.Dispose();
                this.contentBrowserState.PropertyChanged -= this.ContentBrowserState_PropertyChanged;
                this.Query.Changed -= this.OnQueryChanged;
                this.Presentation.SortChanged -= this.OnQueryChanged;
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
    protected void OnItemInvoked(AssetEntry item)
    {
        if (!this.disposed && ReferenceEquals(this.contentBrowserState.ActiveAssetLayout, this)
            && this.Assets.FirstOrDefault(row => row.Item.Identity == item.Identity) is { } current)
        {
            this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(current.Item));
        }
    }

    private static bool HasCookedProjection(AssetEntry asset)
        => asset.PrimaryState is AssetState.Cooked
           || asset.DerivedState is AssetState.Cooked or AssetState.Stale
           || (asset.PrimaryState is AssetState.Broken && asset.IsCookedOnly);

    private void ApplyScriptProblems(AssetBrowserRow row)
    {
        if (scripts is not null && row.Item is { Kind: AssetKind.Script })
        {
            row.SetScriptProblems(scripts.Find(AssetFileTypes.ToIdentity(row.Item.Identity)));
        }
    }

    private void OnScriptChanged(ScriptChangedEventArgs args)
    {
        foreach (var row in this.Assets.Where(row => row.Item is { Kind: AssetKind.Script }
            && (args.Script is null || AssetFileTypes.ToIdentity(row.Item.Identity) == args.Script)))
        {
            this.ApplyScriptProblems(row);
        }
    }

    private async Task InitializeAsync()
    {
        var firstSnapshot = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.initialSnapshotCompletion = firstSnapshot;
        this.subscription?.Dispose();
        this.scriptChanges?.Dispose();
        this.contentBrowserState.PropertyChanged -= this.ContentBrowserState_PropertyChanged;
        this.Query.Changed -= this.OnQueryChanged;
        this.Presentation.SortChanged -= this.OnQueryChanged;
        this.scriptChanges = scripts is null ? null : Observable.FromEventPattern<ScriptChangedEventArgs>(
                handler => scripts.Changed += handler,
                handler => scripts.Changed -= handler)
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(change => this.OnScriptChanged(change.EventArgs));
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
        this.Presentation.SortChanged += this.OnQueryChanged;
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
        }
    }

    private void ReplaceItems(IReadOnlyList<AssetEntry> items)
    {
        this.projectCookedFolders = ProjectCookedFolders.Roots(this.projectContextService.ActiveProject);
        if (this.disposed)
        {
            return;
        }

        this.latestItems = items;
        this.hasSnapshot = true;
        var selectedIdentities = this.IsActiveLayout
            ? this.contentBrowserState.SelectedIdentities : [.. this.selectedAssets.Select(static asset => asset.Identity)];
        var existing = this.Assets.ToDictionary(static row => row.Item.Identity);
        var scopedItems = this.projectContextService.ActiveProject is { } project
            ? CookedLibraryFolders.ForFolders(items, project, this.contentBrowserState.SelectedFolders) : items;
        // A container is one asset, its source file: its products (a model's materials, an input source's
        // actions) are not listed, and pickers and references reach them by identity.
        var inScope = scopedItems.Where(static item => AssetFileTypes.ContainerOf(item.Identity) is null)
            .Where(this.IsInSelectedFolders).ToArray();
        this.scopeCount = inScope.Length;
        var visible = this.Presentation.Sort(inScope.Where(this.Query.Matches));
        this.isReplacingRows = true;
        try
        {
            for (var index = 0; index < visible.Count; index++)
            {
                var item = visible[index];
                if (existing.TryGetValue(item.Identity, out var row))
                {
                    row.Update(item);
                    this.ApplyScriptProblems(row);
                    if (!ReferenceEquals(this.Assets[index], row))
                    {
                        this.Assets.Move(this.Assets.IndexOf(row), index);
                    }
                }
                else
                {
                    var added = new AssetBrowserRow(item);
                    this.ApplyScriptProblems(added);
                    this.Assets.Insert(index, added);
                }
            }

            while (this.Assets.Count > visible.Count)
            {
                this.Assets.RemoveAt(this.Assets.Count - 1);
            }
        }
        finally
        {
            this.isReplacingRows = false;
        }

        this.SetSelection(selectedIdentities.Select(identity => this.FindRow(identity)?.Item).OfType<AssetEntry>());
        this.PublishCounts();
        this.NotifyEmptyState();
    }

    private AssetBrowserRow? FindRow(AssetPath identity)
        => this.Assets.FirstOrDefault(row => row.Item.Identity == identity);

    private void PublishSelection()
    {
        if (this.IsActiveLayout)
        {
            this.contentBrowserState.SelectedIdentity = this.selectedAsset?.Identity;
            this.contentBrowserState.SelectedIdentities = [.. this.selectedAssets.Select(static asset => asset.Identity)];
            this.contentBrowserState.PublishSelection(this.selectedAssets);
        }
    }

    private void PublishCounts()
    {
        if (this.IsActiveLayout)
        {
            this.contentBrowserState.PublishCounts(this.Assets.Count, this.scopeCount);
        }
    }

    private void OnQueryChanged(object? sender, EventArgs args) => this.ReplaceItems(this.latestItems);

    [RelayCommand]
    private void BrowseSource()
    {
        var sourceFolders = this.contentBrowserState.SelectedFolders
            .Select(folder => ProjectCookedFolders.TryMap(folder, this.projectCookedFolders, out var source) ? source : folder)
            .ToArray();
        if (sourceFolders.Any(static folder => folder is null))
        {
            // All of the output browses the sources of every authoring folder.
            this.contentBrowserState.SetSelectedFolders(this.projectContextService.ActiveProject?.AuthoringMounts
                .Where(static mount => !ProjectExplorer.ProjectLayoutViewModel.IsPersistedProjectRelativeVirtualMount(mount))
                .Select(static mount => AssetPath.Create(mount.Name)) ?? []);
        }
        else
        {
            this.contentBrowserState.SetSelectedFolders(sourceFolders.OfType<AssetPath>());
        }
    }

    private bool IsCookedScope()
        => this.contentBrowserState.SelectedFolders.Any(folder => ProjectCookedFolders.TryMap(folder, this.projectCookedFolders, out _));

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

    private bool IsInSelectedFolders(AssetEntry asset)
        => IsInSelectedFolders(
            asset.DisplayPath,
            asset.Identity,
            this.contentBrowserState.SelectedFolders,
            this.projectContextService.ActiveProject is not null,
            HasCookedProjection(asset),
            this.projectCookedFolders);

}
