// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.ContentBrowser.Models;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// A base ViewModel for the assets view layout.
/// </summary>
public abstract class AssetsLayoutViewModel(
    IAssetCatalog assetCatalog,
    IProject currentProject,
    ContentBrowserState contentBrowserState,
    HostingContext hostingContext) : ObservableObject, IRoutingAware, IDisposable
{
    private readonly Dictionary<string, GameAsset> shownByKey = new(StringComparer.OrdinalIgnoreCase);
    private readonly IAssetCatalog assetCatalog = assetCatalog;
    private readonly IProject currentProject = currentProject;
    private readonly ContentBrowserState contentBrowserState = contentBrowserState;
    private readonly HostingContext hostingContext = hostingContext;

    private IDisposable? subscription;
    private bool disposed;
    private bool initialized;

    /// <summary>
    /// Occurs when an item in the assets view is invoked.
    /// </summary>
    public event EventHandler<AssetsViewItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    /// Gets the collection of game assets.
    /// </summary>
    public ObservableCollection<GameAsset> Assets { get; } = [];

    /// <summary>
    /// Forces a refresh of <see cref="Assets"/> by re-querying the catalog and applying the current folder filter.
    /// Useful when external processes (e.g. import/cook) update the cooked index but filesystem watchers don't emit changes.
    /// </summary>
    public Task RefreshAsync() => this.RefreshAssetsAsync();

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

    /// <summary>
    /// Releases the unmanaged resources used by the AssetsLayoutViewModel and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">true to release both managed and unmanaged resources; false to release only unmanaged resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.disposed)
        {
            if (disposing)
            {
                this.subscription?.Dispose();
            }

            this.disposed = true;
        }
    }

    /// <summary>
    /// Invokes the <see cref="ItemInvoked"/> event.
    /// </summary>
    /// <param name="item">The game asset that was invoked.</param>
    protected void OnItemInvoked(GameAsset item) => this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(item));

    /// <summary>
    /// Initializes the assets collection by querying the snapshot and subscribing to changes.
    /// </summary>
    private async Task InitializeAsync()
    {
        // Subscribe to asset changes
        this.subscription = this.assetCatalog.Changes
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(this.OnAssetChange);

        await this.RefreshAssetsAsync().ConfigureAwait(false);

        // Listen for folder selection changes
        this.contentBrowserState.PropertyChanged += this.ContentBrowserState_PropertyChanged;
    }

    private void ContentBrowserState_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            _ = Task.Run(this.RefreshAssetsAsync);
        }
    }

    private async Task RefreshAssetsAsync()
    {
        try
        {
            _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
            {
                this.Assets.Clear();
                this.shownByKey.Clear();
            });

            var query = new AssetQuery(this.BuildScopeForSelection());
            var items = await this.assetCatalog.QueryAsync(query).ConfigureAwait(false);

            // Resolve duplicates across catalog sources (e.g. authoring *.otex.json vs cooked *.otex)
            // by a logical asset key, preferring authoring sources.
            var selected = new Dictionary<string, GameAsset>(StringComparer.OrdinalIgnoreCase);

            foreach (var item in items)
            {
                var asset = this.CreateGameAsset(item.Uri);
                if (asset == null || !this.IsInSelectedFolders(asset))
                {
                    continue;
                }

                var key = GetLogicalAssetKey(asset);
                if (!selected.TryGetValue(key, out var existing))
                {
                    selected[key] = asset;
                    continue;
                }

                if (IsPreferredOver(asset, existing))
                {
                    selected[key] = asset;
                }
            }

            _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
            {
                foreach (var asset in selected.Values.OrderBy(a => a.VirtualPath ?? a.Location, StringComparer.OrdinalIgnoreCase))
                {
                    this.Assets.Add(asset);
                    this.shownByKey[GetLogicalAssetKey(asset)] = asset;
                }
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[{this.GetType().Name}] Error refreshing assets: {ex.Message}");
        }

        static bool IsPreferredOver(GameAsset candidate, GameAsset existing)
        {
            // Prefer authoring sources (compound extensions like "*.otex.json") over cooked/runtime artifacts.
            var candidateIsAuthoring = IsAuthoringSource(candidate.VirtualPath);
            var existingIsAuthoring = IsAuthoringSource(existing.VirtualPath);

            if (candidateIsAuthoring != existingIsAuthoring)
            {
                return candidateIsAuthoring;
            }

            // Otherwise keep the existing deterministic choice.
            return false;
        }

        static bool IsAuthoringSource(string? virtualPath)
        {
            if (string.IsNullOrEmpty(virtualPath))
            {
                return false;
            }

            return virtualPath.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase);
        }

        static string GetLogicalAssetKey(GameAsset asset)
        {
            var path = asset.VirtualPath ?? asset.Location;
            if (string.IsNullOrEmpty(path))
            {
                return string.Empty;
            }

            // Normalize known authoring sources to their runtime/canonical extension.
            // Example: "/Content/Textures/Wood.otex.json" -> "/Content/Textures/Wood.otex"
            if (path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase))
            {
                return path[..^".json".Length];
            }

            if (path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
            {
                return path[..^".json".Length];
            }

            if (path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase))
            {
                return path[..^".json".Length];
            }

            if (path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase))
            {
                return path[..^".json".Length];
            }

            return path;
        }
    }

    private void OnAssetChange(AssetChange change)
    {
        var asset = this.CreateGameAsset(change.Uri);
        if (asset == null)
        {
            return;
        }

        var key = GetLogicalAssetKey(asset);

        _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
        {
            switch (change.Kind)
            {
                case AssetChangeKind.Added:
                    if (!this.IsInSelectedFolders(asset))
                    {
                        break;
                    }

                    if (!this.shownByKey.TryGetValue(key, out var existing))
                    {
                        this.Assets.Add(asset);
                        this.shownByKey[key] = asset;
                        break;
                    }

                    // If we already show an item for this logical key, prefer authoring source.
                    if (IsAuthoringSource(asset.VirtualPath) && !IsAuthoringSource(existing.VirtualPath))
                    {
                        var idx = this.Assets.IndexOf(existing);
                        if (idx >= 0)
                        {
                            this.Assets[idx] = asset;
                        }
                        else
                        {
                            this.Assets.Add(asset);
                        }

                        this.shownByKey[key] = asset;
                    }

                    break;

                case AssetChangeKind.Removed:
                    if (this.shownByKey.TryGetValue(key, out var shown) && string.Equals(shown.VirtualPath, asset.VirtualPath, StringComparison.OrdinalIgnoreCase))
                    {
                        _ = this.Assets.Remove(shown);
                        _ = this.shownByKey.Remove(key);
                    }

                    break;

                case AssetChangeKind.Updated:
                    // Handle update if needed
                    break;
            }
        });

        static bool IsAuthoringSource(string? virtualPath)
        {
            if (string.IsNullOrEmpty(virtualPath))
            {
                return false;
            }

            return virtualPath.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
                   || virtualPath.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase);
        }

        static string GetLogicalAssetKey(GameAsset asset)
        {
            var path = asset.VirtualPath ?? asset.Location;
            if (string.IsNullOrEmpty(path))
            {
                return string.Empty;
            }

            if (path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
                || path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase))
            {
                return path[..^".json".Length];
            }

            return path;
        }
    }

    private AssetQueryScope BuildScopeForSelection()
    {
        var folders = this.contentBrowserState.SelectedFolders;
        if (folders is null || folders.Count == 0)
        {
            return AssetQueryScope.All;
        }

        // If root is selected, show everything
        if (folders.Contains(".") || folders.Contains("/"))
        {
            return AssetQueryScope.All;
        }

        var roots = new List<Uri>();
        foreach (var folder in folders)
        {
            if (string.IsNullOrWhiteSpace(folder))
            {
                continue;
            }

            var normalized = folder.Replace('\\', '/');
            if (!normalized.StartsWith('/'))
            {
                // Fallback for legacy/non-canonical paths: assume "project" mount.
                roots.Add(AssetUriHelper.CreateUri("project", normalized));
                continue;
            }

            // Expected canonical absolute folder virtual path: "/<Mount>/<Optional/Path>"
            var segments = normalized.Split('/', StringSplitOptions.RemoveEmptyEntries);
            if (segments.Length == 0)
            {
                continue;
            }

            var mountPoint = segments[0];
            var relative = segments.Length == 1 ? string.Empty : string.Join('/', segments.Skip(1));
            roots.Add(AssetUriHelper.CreateUri(mountPoint, relative));
        }

        return roots.Count == 0
            ? AssetQueryScope.All
            : new AssetQueryScope(roots, AssetQueryTraversal.Descendants);
    }

    private GameAsset? CreateGameAsset(Uri uri)
    {
        string virtualPath;
        string location;
        var name = Uri.UnescapeDataString(Path.GetFileNameWithoutExtension(uri.AbsolutePath));
        var mountPoint = AssetUriHelper.GetMountPoint(uri);

        if (mountPoint.Equals("project", StringComparison.OrdinalIgnoreCase))
        {
            virtualPath = "/" + AssetUriHelper.GetRelativePath(uri);
            if (this.currentProject.ProjectInfo.Location != null)
            {
                location = Path.Combine(this.currentProject.ProjectInfo.Location, AssetUriHelper.GetRelativePath(uri));
            }
            else
            {
                return null;
            }
        }
        else
        {
            virtualPath = AssetUriHelper.GetVirtualPath(uri);
            var mount = this.currentProject.ProjectInfo.AuthoringMounts.FirstOrDefault(m => m.Name.Equals(mountPoint, StringComparison.OrdinalIgnoreCase));
            if (mount != null && this.currentProject.ProjectInfo.Location != null)
            {
                location = Path.Combine(this.currentProject.ProjectInfo.Location, mount.RelativePath, AssetUriHelper.GetRelativePath(uri));
            }
            else
            {
                var localMount = this.currentProject.ProjectInfo.LocalFolderMounts.FirstOrDefault(m => m.Name.Equals(mountPoint, StringComparison.OrdinalIgnoreCase));
                if (localMount != null)
                {
                    location = Path.Combine(localMount.AbsolutePath, AssetUriHelper.GetRelativePath(uri));
                }
                else
                {
                    return null;
                }
            }
        }

        return new GameAsset(name, location)
        {
            VirtualPath = virtualPath,
            AssetType = GameAsset.GetAssetType(Uri.UnescapeDataString(uri.AbsolutePath)),
        };
    }

    private bool IsInSelectedFolders(GameAsset asset)
    {
        var folders = this.contentBrowserState.SelectedFolders;
        if (folders == null || folders.Count == 0)
        {
            return true;
        }

        // If root is selected, show everything
        if (folders.Contains(".") || folders.Contains("/"))
        {
            return true;
        }

        if (string.IsNullOrEmpty(asset.VirtualPath))
        {
            return false;
        }

        foreach (var sf in folders)
        {
            if (string.IsNullOrWhiteSpace(sf))
            {
                continue;
            }

            // Normalize selected folder path to match VirtualPath format (start with /)
            var normalizedSf = sf.Replace('\\', '/');
            if (!normalizedSf.StartsWith('/'))
            {
                normalizedSf = "/" + normalizedSf;
            }

            if (string.Equals(asset.VirtualPath, normalizedSf, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (asset.VirtualPath.StartsWith(normalizedSf + "/", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}
