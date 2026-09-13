// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
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
    private bool initialized;
    private IReadOnlyList<ContentBrowserAssetItem> latestItems = [];
    private ContentBrowserAssetItem? selectedAsset;

    /// <summary>
    /// Occurs when an item in the assets view is invoked.
    /// </summary>
    public event EventHandler<AssetsViewItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    /// Gets the collection of content browser asset rows.
    /// </summary>
    public ObservableCollection<AssetBrowserRow> Assets { get; } = [];

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
        set => this.SetProperty(ref this.selectedAsset, value);
    }

    /// <summary>
    /// Forces a refresh of <see cref="Assets"/> by re-querying the provider.
    /// </summary>
    /// <returns>Completion of the shared refresh.</returns>
    public Task RefreshAsync() => this.assetProvider.RefreshAsync(AssetBrowserFilter.Default);

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        if (!this.initialized)
        {
            await this.InitializeAsync().ConfigureAwait(false);
            this.initialized = true;
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
    /// <returns>Whether the asset belongs to this scope.</returns>
    internal static bool IsInSelectedFolders(
        string displayPath,
        string identityAbsolutePath,
        IReadOnlyCollection<string> selectedFolders,
        bool hasActiveProject,
        string? cookedAbsolutePath = null,
        bool hasCookedProjection = false)
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
            if (TryMapCookedSelectionToRuntimePath(selected, out var cookedSelection))
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
                this.subscription?.Dispose();
                this.builtinChanges?.Dispose();
                this.contentBrowserState.PropertyChanged -= this.ContentBrowserState_PropertyChanged;
            }

            this.disposed = true;
        }
    }

    /// <summary>
    /// Invokes the <see cref="ItemInvoked"/> event.
    /// </summary>
    /// <param name="item">The asset row that was invoked.</param>
    protected void OnItemInvoked(ContentBrowserAssetItem item)
        => this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(item));

    private static bool HasCookedProjection(ContentBrowserAssetItem asset)
        => asset.PrimaryState is AssetState.Cooked
           || asset.DerivedState is AssetState.Cooked or AssetState.Stale
           || (asset.PrimaryState is AssetState.Broken && asset.CookedUri == asset.IdentityUri);

    private static string NormalizeFolderPath(string value)
    {
        var normalized = value.Replace('\\', '/').Trim('/');
        return string.IsNullOrEmpty(normalized) ? "/" : "/" + normalized;
    }

    private static bool IsSameOrChildPath(string candidate, string folder)
    {
        var normalizedCandidate = NormalizeFolderPath(candidate);
        return normalizedCandidate.Equals(folder, StringComparison.OrdinalIgnoreCase)
               || normalizedCandidate.StartsWith(folder + "/", StringComparison.OrdinalIgnoreCase);
    }

    private static bool TryMapCookedSelectionToRuntimePath(string selectedFolder, out string runtimePath)
    {
        runtimePath = string.Empty;
        var normalized = NormalizeFolderPath(selectedFolder);
        const string cookedRoot = "/Cooked";
        if (!normalized.Equals(cookedRoot, StringComparison.OrdinalIgnoreCase)
            && !normalized.StartsWith(cookedRoot + "/", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        runtimePath = normalized.Length == cookedRoot.Length ? "/" : normalized[cookedRoot.Length..];
        return true;
    }

    private async Task InitializeAsync()
    {
        this.builtinChanges = Observable.FromEventPattern(
                handler => this.builtins.Changed += handler,
                handler => this.builtins.Changed -= handler)
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(_ => this.NotifyBuiltinStatus());
        this.subscription = this.assetProvider.Items
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(this.ReplaceItems);
        this.contentBrowserState.PropertyChanged += this.ContentBrowserState_PropertyChanged;
        await this.RefreshAsync().ConfigureAwait(false);
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
        this.latestItems = items;
        var selectedUri = this.SelectedAsset?.IdentityUri;
        var existing = this.Assets.ToDictionary(static row => row.Item.IdentityUri.AbsoluteUri, StringComparer.OrdinalIgnoreCase);
        var visible = items.Where(this.IsInSelectedFolders).ToArray();
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

        this.SelectedAsset = visible.FirstOrDefault(item => string.Equals(item.IdentityUri.AbsoluteUri, selectedUri?.AbsoluteUri, StringComparison.OrdinalIgnoreCase));
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
            HasCookedProjection(asset));
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
