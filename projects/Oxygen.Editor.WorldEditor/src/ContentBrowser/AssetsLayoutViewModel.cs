// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// A base ViewModel for the assets view layout.
/// </summary>
public abstract class AssetsLayoutViewModel(IAssetIndexingService assetsIndexingService, ContentBrowserState contentBrowserState, HostingContext hostingContext) : ObservableObject, IRoutingAware, IDisposable
{
    private readonly HashSet<string> seenLocations = [];
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
        // Subscribe to asset changes first (so replay buffer is received)
        this.subscription = assetsIndexingService.AssetChanges
            .ObserveOn(this.hostingContext.DispatcherScheduler)
            .Subscribe(
                notification =>
                {
                    // Only handle notifications that match the current selected folders
                    if (!this.IsInSelectedFolders(notification.Asset))
                    {
                        return;
                    }

                    switch (notification.ChangeType)
                    {
                        case AssetChangeType.Added:
                            // Deduplicate replay buffer
                            if (!this.seenLocations.Contains(notification.Asset.Location))
                            {
                                // Modify UI-bound collection on UI dispatcher
                                _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
                                {
                                    this.Assets.Add(notification.Asset);
                                    this.seenLocations.Add(notification.Asset.Location);
                                });
                            }

                            break;

                        case AssetChangeType.Removed:
                            var toRemove = this.Assets.FirstOrDefault(a =>
                                a.Location.Equals(notification.Asset.Location, StringComparison.OrdinalIgnoreCase));
                            if (toRemove != null)
                            {
                                _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
                                {
                                    _ = this.Assets.Remove(toRemove);
                                    _ = this.seenLocations.Remove(notification.Asset.Location);
                                });
                            }

                            break;

                        case AssetChangeType.Modified:
                            // Could update metadata if needed
                            Debug.WriteLine($"[{this.GetType().Name}] Asset modified: {notification.Asset.Name}");
                            break;
                    }
                },
                ex => Debug.WriteLine($"[{this.GetType().Name}] Error in asset stream: {ex.Message}"));

        // Start background task to obtain initial snapshot (non-blocking for UI)
        _ = Task.Run(async () =>
        {
            try
            {
                // Build predicate based on current selected folders
                Func<GameAsset, bool>? predicate = null;
                var folders = this.contentBrowserState.SelectedFolders?.ToList();
                if (folders?.Count > 0 && !folders.Contains(".", StringComparer.Ordinal))
                {
                    predicate = a => this.IsInSelectedFolders(a);
                }

                var snapshot = await assetsIndexingService.QueryAssetsAsync(predicate).ConfigureAwait(false);
                Debug.WriteLine($"[{this.GetType().Name}] Initial snapshot: {snapshot.Count} assets");

                // Merge snapshot into collection with deduplication
                foreach (var asset in snapshot)
                {
                    if (!this.IsInSelectedFolders(asset))
                    {
                        continue;
                    }

                    if (this.seenLocations.Contains(asset.Location))
                    {
                        continue;
                    }

                    var a = asset;
                    _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
                    {
                        this.Assets.Add(a);
                        this.seenLocations.Add(a.Location);
                    });
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[{this.GetType().Name}] Error obtaining initial snapshot: {ex.Message}");
            }
        });

        // Listen for folder selection changes
        this.contentBrowserState.PropertyChanged += this.ContentBrowserState_PropertyChanged;
    }

    private void ContentBrowserState_PropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            _ = Task.Run(this.RefreshForSelectedFoldersAsync);
        }
    }

    private async Task RefreshForSelectedFoldersAsync()
    {
        try
        {
            // Clear current view and rebuild from snapshot filtered by selected folders
            // Must clear on UI thread
            _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
            {
                this.Assets.Clear();
                this.seenLocations.Clear();
            });

            Func<GameAsset, bool>? predicate = null;
            var folders = this.contentBrowserState.SelectedFolders?.ToList();
            if (folders?.Count > 0 && !folders.Contains(".", StringComparer.Ordinal))
            {
                predicate = this.IsInSelectedFolders;
            }

            var snapshot = await assetsIndexingService.QueryAssetsAsync(predicate).ConfigureAwait(false);
            foreach (var asset in snapshot)
            {
                if (!this.IsInSelectedFolders(asset))
                {
                    continue;
                }

                var a = asset;
                _ = this.hostingContext.Dispatcher.DispatchAsync(() =>
                {
                    this.Assets.Add(a);
                    this.seenLocations.Add(a.Location);
                });
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[{this.GetType().Name}] Error refreshing selected folders: {ex.Message}");
        }
    }

    private bool IsInSelectedFolders(GameAsset asset)
    {
        var folders = this.contentBrowserState.SelectedFolders;
        if (folders == null || folders.Count == 0)
        {
            return true;
        }

        if (folders.Contains("."))
        {
            return true;
        }

        // Use project-root-relative, path-segment-aware matching instead of naive substring checks.
        var projectRoot = this.contentBrowserState.ProjectRootPath ?? string.Empty;
        if (string.IsNullOrWhiteSpace(projectRoot))
        {
            return true;
        }

        string relative;
        try
        {
            var assetFull = Path.GetFullPath(asset.Location);
            var projectFull = Path.GetFullPath(projectRoot);
            relative = Path.GetRelativePath(projectFull, assetFull);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[AssetsLayoutViewModel] Path resolution failed for asset '{asset?.Location}' with project root '{projectRoot}': {ex}");
            return false;
        }

        // If asset is not under project root (starts with ".."), do not consider it a match.
        if (relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) || string.Equals(relative, "..", StringComparison.Ordinal))
        {
            return false;
        }

        static string NormalizeForCompare(string p)
            => p.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar).TrimStart('.', Path.DirectorySeparatorChar);

        var relNorm = NormalizeForCompare(relative);

        foreach (var sf in folders)
        {
            if (string.IsNullOrWhiteSpace(sf))
            {
                continue;
            }

            var sel = NormalizeForCompare(sf);

            if (string.Equals(relNorm, sel, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (relNorm.StartsWith(sel + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }
}
