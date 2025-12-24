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
    private readonly HashSet<string> seenLocations = [];
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
                this.seenLocations.Clear();
            });

            var items = await this.assetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);

            foreach (var item in items)
            {
                var asset = this.CreateGameAsset(item.Uri);
                if (asset == null || !this.IsInSelectedFolders(asset))
                {
                    continue;
                }

                _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
                {
                    this.Assets.Add(asset);
                    this.seenLocations.Add(asset.Location);
                });
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[{this.GetType().Name}] Error refreshing assets: {ex.Message}");
        }
    }

    private void OnAssetChange(AssetChange change)
    {
        var asset = this.CreateGameAsset(change.Uri);
        if (asset == null)
        {
            return;
        }

        _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
        {
            switch (change.Kind)
            {
                case AssetChangeKind.Added:
                    if (this.IsInSelectedFolders(asset) && !this.seenLocations.Contains(asset.Location))
                    {
                        this.Assets.Add(asset);
                        this.seenLocations.Add(asset.Location);
                    }

                    break;

                case AssetChangeKind.Removed:
                    var toRemove = this.Assets.FirstOrDefault(a => string.Equals(a.Location, asset.Location, StringComparison.Ordinal));
                    if (toRemove != null)
                    {
                        _ = this.Assets.Remove(toRemove);
                        _ = this.seenLocations.Remove(asset.Location);
                    }

                    break;

                case AssetChangeKind.Updated:
                    // Handle update if needed
                    break;
            }
        });
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
