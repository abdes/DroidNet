// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using IContainer = DryIoc.IContainer;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>
///     The ViewModel for the <see cref="ContentBrowserView" /> view.
/// </summary>
/// <remarks>
///     This is an <see cref="IOutletContainer" /> with two outlets, the "left" outlet for the left pane, and the "right"
///     outlet for
///     the right pane.
/// </remarks>
public sealed partial class ContentBrowserViewModel(
    IContainer container,
    IRouter parentRouter,
    ILoggerFactory? loggerFactory = null) : AbstractOutletContainer, IRoutingAware
{
    private static readonly Routes RoutesConfig = new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            Children = new Routes(
            [
                new Route { Outlet = "left", Path = "project", ViewModelType = typeof(ProjectLayoutViewModel) },
                new Route
                {
                    Path = string.Empty,
                    Outlet = "right",
                    ViewModelType = typeof(AssetsViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "assets/tiles", Outlet = "right", ViewModelType = typeof(TilesLayoutViewModel),
                        },
                        new Route
                        {
                            Path = "assets/list", Outlet = "right", ViewModelType = typeof(ListLayoutViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    private static readonly char[] AnyPathSeparator = ['\\', '/'];

    private readonly ILogger logger = loggerFactory?.CreateLogger<ContentBrowserViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ContentBrowserViewModel>();

    // Navigation history management
    private readonly List<string> navigationHistory = [];

    private IContainer? childContainer;
    private string currentAssetsViewPath = "assets/tiles"; // tracks whether we show list or tiles
    private int currentHistoryIndex = -1;
    private DispatcherQueue? dispatcher;

    private bool isDisposed;
    private bool isInitialized;
    private bool isNavigatingFromHistory;

    private IRouter? localRouter;
    private IDisposable? routerEventsSubscription;

    /// <summary>
    ///     Gets a value indicating whether a refresh is currently in progress.
    ///     Used to disable refresh command during execution.
    /// </summary>
    [ObservableProperty]
    public partial bool IsRefreshing { get; set; }

    // Breadcrumbs
    [ObservableProperty]
    public partial ObservableCollection<BreadcrumbEntry> Breadcrumbs { get; set; } = [];

    /// <summary>
    ///     Gets a value indicating whether back navigation is possible.
    /// </summary>
    [ObservableProperty]
    public partial bool CanGoBack { get; set; }

    /// <summary>
    ///     Gets a value indicating whether forward navigation is possible.
    /// </summary>
    [ObservableProperty]
    public partial bool CanGoForward { get; set; }

    /// <summary>
    ///     Gets a value indicating whether navigating up is possible.
    /// </summary>
    [ObservableProperty]
    public partial bool CanGoUp { get; set; }

    /// <summary>
    ///     Gets the local ViewModel to View converter. Guaranteed to be not <see langword="null" /> when the view is loaded.
    /// </summary>
    public ViewModelToView? VmToViewConverter { get; private set; }

    /// <summary>
    ///     Gets the ViewModel for the left pane.
    /// </summary>
    public object? LeftPaneViewModel => this.Outlets["left"].viewModel;

    /// <summary>
    ///     Gets the ViewModel for the right pane.
    /// </summary>
    public object? RightPaneViewModel => this.Outlets["right"].viewModel;

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // One-time initialization for singleton
        if (!this.isInitialized)
        {
            this.Outlets.Add("left", (nameof(this.LeftPaneViewModel), null));
            this.Outlets.Add("right", (nameof(this.RightPaneViewModel), null));

            InitializeChildContainer();

            // Make sure to use our own ViewModel to View converter
            this.VmToViewConverter = this.childContainer.Resolve<ViewModelToView>();

            this.localRouter = this.childContainer.Resolve<IRouter>();

            // Capture the UI Dispatcher from the hosting context to marshal UI-bound updates
            var hostingContext = this.childContainer.Resolve<HostingContext>();
            this.dispatcher = hostingContext.Dispatcher;

            // Subscribe to router events to track navigation history
            this.routerEventsSubscription = this.localRouter.Events
                .OfType<NavigationEnd>()
                .Subscribe(this.OnNavigationEnd);

            // Subscribe to ContentBrowserState changes to update the navigation stack
            var contentBrowserState = this.childContainer.Resolve<ContentBrowserState>();
            contentBrowserState.PropertyChanged += this.OnContentBrowserStateChanged;

            // Start asset indexing in background now that project is loaded
            var assetIndexer = this.childContainer.Resolve<IAssetIndexingService>();
            _ = assetIndexer.StartIndexingAsync();

            var initialUrl = "/(left:project//right:" + this.currentAssetsViewPath + ")";
            await this.localRouter.NavigateAsync(initialUrl).ConfigureAwait(true);

            this.isInitialized = true;

            this.UpdateUpButtonState();
            this.UpdateBreadcrumbs();
        }

        void InitializeChildContainer()
        {
            this.childContainer = container
                .WithRegistrationsCopy()
                .WithMvvm()
                .WithLocalRouting(
                    RoutesConfig,
                    new LocalRouterContext(navigationContext.NavigationTarget)
                    {
                        ParentRouter = parentRouter,
                        RootViewModel = this,
                    });

            this.childContainer.Register<ContentBrowserState>(Reuse.Singleton);

            this.childContainer.Register<ProjectLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<ProjectLayoutView>(Reuse.Singleton);
            this.childContainer.Register<AssetsViewModel>(Reuse.Singleton);
            this.childContainer.Register<AssetsView>(Reuse.Singleton);
            this.childContainer.Register<ListLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<ListLayoutView>(Reuse.Singleton);
            this.childContainer.Register<TilesLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<TilesLayoutView>(Reuse.Singleton);
        }
    }

    /// <summary>
    ///     Navigate to a breadcrumb at the given index.
    ///     Called by the view's breadcrumb ItemClicked handler.
    /// </summary>
    /// <param name="index">The zero-based index of the breadcrumb to navigate to.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous navigation operation.</returns>
    public async Task NavigateToBreadcrumbAsync(int index)
    {
        if (index < 0 || index >= this.Breadcrumbs.Count || this.localRouter is null)
        {
            return;
        }

        var target = this.Breadcrumbs[index];
        var url = "/(left:project//right:" + this.currentAssetsViewPath + ")";
        if (string.Equals(target.RelativePath, ".", StringComparison.Ordinal))
        {
            url += "?selected=.";
        }
        else
        {
            url += $"?selected={Uri.EscapeDataString(target.RelativePath)}";
        }

        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    /// <inheritdoc />
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.isDisposed = true;
            this.routerEventsSubscription?.Dispose();

            // Unsubscribe from ContentBrowserState
            if (this.childContainer != null)
            {
                var contentBrowserState = this.childContainer.Resolve<ContentBrowserState>();
                contentBrowserState.PropertyChanged -= this.OnContentBrowserStateChanged;
            }

            this.childContainer?.Dispose();
        }

        base.Dispose(disposing);
    }



    private static string? GetParentRelativePath(string? relativePath)
    {
        if (string.IsNullOrEmpty(relativePath) || string.Equals(relativePath, ".", StringComparison.Ordinal))
        {
            return null; // No parent above root
        }

        // Handle both Windows and URL style separators
        var idx = relativePath.LastIndexOfAny(AnyPathSeparator);
        if (idx < 0)
        {
            return string.Empty; // Parent is project root
        }

        return idx == 0 ? string.Empty : relativePath[..idx];
    }

    private static string? GetPrimarySelectedFolder(ContentBrowserState? state)
    {
        if (state is null || state.SelectedFolders.Count == 0)
        {
            return null;
        }

        // Choose a deterministic primary folder when multiple are selected: the first in ordinal order
        return state.SelectedFolders.Order(StringComparer.Ordinal).FirstOrDefault();
    }

    private void OnContentBrowserStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal) && !this.isNavigatingFromHistory)
        {
            // Build the new URL for history tracking but DON'T navigate
            var currentUrl = this.BuildCurrentUrl();

            this.LogContentBrowserStateChanged(currentUrl);

            // Only update history, don't trigger router navigation
            this.UpdateHistoryForStateChange(currentUrl);

            // Update the Up button state based on selection
            this.UpdateUpButtonState();

            // Update breadcrumbs
            this.UpdateBreadcrumbs();
        }
    }

    private string BuildCurrentUrl()
    {
        var contentBrowserState = this.childContainer?.Resolve<ContentBrowserState>();
        if (contentBrowserState == null)
        {
            return "/(left:project//right:" + this.currentAssetsViewPath + ")";
        }

        if (contentBrowserState.SelectedFolders.Count == 0)
        {
            // No folders selected - return URL without query parameters
            return "/(left:project//right:" + this.currentAssetsViewPath + ")";
        }

        var query = RouteStateMapping.BuildSelectedQuery(contentBrowserState.SelectedFolders);
        return $"/(left:project//right:{this.currentAssetsViewPath}){query}";
    }

    private void UpdateHistoryForStateChange(string url)
    {
        // Only update history if this is a new URL
        if (this.navigationHistory.Count == 0 || !string.Equals(this.navigationHistory[this.currentHistoryIndex], url, StringComparison.Ordinal))
        {
            this.AddToHistory(url);
        }
    }

    private void OnNavigationEnd(NavigationEnd navigationEnd)
    {
        this.LogNavigationEnd(navigationEnd.Url ?? string.Empty, this.isNavigatingFromHistory);

        // Track current assets view path from the URL (assets/list or assets/tiles)
        var url = navigationEnd.Url ?? string.Empty;
        if (url.Contains("assets/tiles", StringComparison.Ordinal))
        {
            this.currentAssetsViewPath = "assets/tiles";
        }
        else if (url.Contains("assets/list", StringComparison.Ordinal))
        {
            this.currentAssetsViewPath = "assets/list";
        }

        if (!this.isNavigatingFromHistory && !string.IsNullOrEmpty(navigationEnd.Url))
        {
            // Avoid pushing duplicate if it's identical to the current entry
            if (this.navigationHistory.Count == 0 ||
!string.Equals(this.navigationHistory[this.currentHistoryIndex], navigationEnd.Url, StringComparison.Ordinal))
            {
                this.AddToHistory(navigationEnd.Url);
            }
            else
            {
                this.LogHistoryDuplicateSkipped();
            }
        }

        this.UpdateHistoryButtonStates();

        // Ensure UI elements that depend on the current location update immediately.
        // Rebuild breadcrumbs based on the URL we just navigated to. This avoids waiting
        // for state propagation when navigation was initiated via router/URL (e.g., breadcrumb click).
        var selectedFromUrl = RouteStateMapping.ParseFirstSelectedFromUrl(navigationEnd.Url);
        this.UpdateBreadcrumbs(selectedFromUrl);
    }

    private void AddToHistory(string url)
    {
        this.LogHistoryPush(url);

        // If the last entry equals the new URL, skip to prevent duplicates
        if (this.navigationHistory.Count > 0 && string.Equals(this.navigationHistory[this.currentHistoryIndex], url, StringComparison.Ordinal))
        {
            this.LogHistoryDuplicateSkipped();
            this.UpdateHistoryButtonStates();
            return;
        }

        // Remove any forward history if we're navigating to a new location
        if (this.currentHistoryIndex < this.navigationHistory.Count - 1)
        {
            var removedCount = this.navigationHistory.Count - this.currentHistoryIndex - 1;
            this.LogHistoryForwardCleared(removedCount, this.currentHistoryIndex + 1);
            this.navigationHistory.RemoveRange(this.currentHistoryIndex + 1, removedCount);
        }

        // Add new URL to history
        this.navigationHistory.Add(url);
        this.currentHistoryIndex = this.navigationHistory.Count - 1;

        this.LogHistoryState(this.navigationHistory.Count, this.currentHistoryIndex);
        this.LogHistoryFull(this.navigationHistory, this.currentHistoryIndex);

        this.UpdateHistoryButtonStates();
    }

    private void UpdateHistoryButtonStates()
    {
        this.CanGoBack = this.currentHistoryIndex > 0;
        this.CanGoForward = this.currentHistoryIndex < this.navigationHistory.Count - 1;

        this.LogHistoryState(this.navigationHistory.Count, this.currentHistoryIndex);

        // Notify the commands that their CanExecute state may have changed
        this.GoBackCommand.NotifyCanExecuteChanged();
        this.GoForwardCommand.NotifyCanExecuteChanged();

        // Up button depends on current selection, update it too
        this.UpdateUpButtonState();
    }

    /// <summary>
    ///     Navigates back in the history.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoBack))]
    private async Task GoBackAsync()
    {
        if (this.CanGoBack && this.localRouter != null)
        {
            this.currentHistoryIndex--;
            var targetUrl = this.navigationHistory[this.currentHistoryIndex];
            this.LogNavigationBack(this.currentHistoryIndex + 1, this.currentHistoryIndex, targetUrl);

            this.isNavigatingFromHistory = true;

            try
            {
                await this.localRouter.NavigateAsync(targetUrl).ConfigureAwait(true);
            }
            finally
            {
                this.isNavigatingFromHistory = false;
                this.UpdateHistoryButtonStates();
            }
        }
    }

    /// <summary>
    ///     Navigates forward in the history.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoForward))]
    private async Task GoForwardAsync()
    {
        if (this.CanGoForward && this.localRouter != null)
        {
            this.currentHistoryIndex++;
            var targetUrl = this.navigationHistory[this.currentHistoryIndex];
            this.LogNavigationForward(this.currentHistoryIndex - 1, this.currentHistoryIndex, targetUrl);

            this.isNavigatingFromHistory = true;

            try
            {
                await this.localRouter.NavigateAsync(this.navigationHistory[this.currentHistoryIndex])
                    .ConfigureAwait(true);
            }
            finally
            {
                this.isNavigatingFromHistory = false;
                this.UpdateHistoryButtonStates();
            }
        }
    }

    /// <summary>
    ///     Navigates to the parent of the currently selected folder in the project tree.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoUp))]
    private async Task GoUpAsync()
    {
        if (this.localRouter is null)
        {
            return;
        }

        var state = this.childContainer?.Resolve<ContentBrowserState>();
        var current = GetPrimarySelectedFolder(state);

        // If no current selection, nothing to do
        if (string.IsNullOrEmpty(current))
        {
            return;
        }

        var parent = GetParentRelativePath(current);

        // If parent is null, we're already at root (no action)
        if (parent is null)
        {
            return;
        }

        var url = "/(left:project//right:" + this.currentAssetsViewPath + ")";
        if (parent.Length > 0)
        {
            url += $"?selected={Uri.EscapeDataString(parent)}";
        }
        else
        {
            // Parent is the project root; represent it explicitly as selected=.
            url += "?selected=.";
        }

        this.LogNavigationUp(url, current);
        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    private void UpdateUpButtonState()
    {
        var state = this.childContainer?.Resolve<ContentBrowserState>();
        var current = GetPrimarySelectedFolder(state);
        var parent = current is not null ? GetParentRelativePath(current) : null;

        var old = this.CanGoUp;
        this.CanGoUp = parent is not null;
        if (old != this.CanGoUp)
        {
            this.LogCanGoUpUpdated(this.CanGoUp, current, parent);
        }

        this.GoUpCommand.NotifyCanExecuteChanged();
    }

    private void UpdateBreadcrumbs(string? pathOverride = null)
    {
        // Build the new list fully, then swap the collection on the UI thread to avoid per-item change notifications
        void UpdateCore()
        {
            var entries = new List<BreadcrumbEntry>();

            var primary = pathOverride;
            if (primary is null)
            {
                var state = this.childContainer?.Resolve<ContentBrowserState>();
                primary = GetPrimarySelectedFolder(state);
            }

            // Determine project name for root label if available
            var rootLabel = "Project";
            try
            {
                var pm = this.childContainer?.Resolve<IProjectManagerService>();
                var pn = pm?.CurrentProject?.ProjectInfo?.Name;
                if (!string.IsNullOrEmpty(pn))
                {
                    rootLabel = pn!;
                }
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                // ignore, fallback to default label
                this.LogProjectNameRetrievalFailed(ex);
            }
#pragma warning restore CA1031 // Do not catch general exception types

            // Always include root breadcrumb
            entries.Add(new BreadcrumbEntry(rootLabel, "."));

            if (!string.IsNullOrEmpty(primary) && !string.Equals(primary, ".", StringComparison.Ordinal))
            {
                // Split path and accumulate segments
                var parts = primary.Replace('\\', '/').Split('/', StringSplitOptions.RemoveEmptyEntries);
                var pathSoFar = new List<string>();
                foreach (var part in parts)
                {
                    pathSoFar.Add(part);
                    var rel = string.Join('/', pathSoFar);
                    entries.Add(new BreadcrumbEntry(part, rel));
                }
            }

            // Swap the entire collection to minimize WinRT collection change handling issues
            this.Breadcrumbs = new ObservableCollection<BreadcrumbEntry>(entries);
        }

        // Always enqueue to the UI dispatcher to avoid re-entrancy during input/layout handlers
        if (this.dispatcher is not null)
        {
            _ = this.dispatcher.DispatchAsync(UpdateCore);
        }
        else
        {
            UpdateCore();
        }
    }

    /// <summary>
    ///     Switch the assets area to list view (details).
    /// </summary>
    [RelayCommand]
    private async Task SwitchToListViewAsync()
    {
        if (this.localRouter is null)
        {
            return;
        }

        this.currentAssetsViewPath = "assets/list";
        var url = this.BuildCurrentUrl();
        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    /// <summary>
    ///     Switch the assets area to detail view (tiles grid).
    /// </summary>
    [RelayCommand]
    private async Task SwitchToDetailViewAsync()
    {
        if (this.localRouter is null)
        {
            return;
        }

        this.currentAssetsViewPath = "assets/tiles";
        var url = this.BuildCurrentUrl();
        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    /// <summary>
    ///     Forces re-indexing of assets for the currently selected folders.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanExecuteRefresh))]
    private async Task RefreshAsync()
    {
        if (this.childContainer is null)
        {
            return;
        }

        if (!this.CanExecuteRefresh())
        {
            return;
        }

        try
        {
            this.IsRefreshing = true;
            this.RefreshCommand.NotifyCanExecuteChanged();
            this.LogRefreshRequested();

            // Refresh the project explorer tree first
            var projectLayout = this.childContainer.Resolve<ProjectLayoutViewModel>();
            await projectLayout.RefreshTreeAsync().ConfigureAwait(true);

            // Asset indexing runs automatically in background with file watching
            // No manual refresh needed - just wait a moment for UI update
            await Task.Delay(100).ConfigureAwait(true);
        }
        finally
        {
            this.IsRefreshing = false;
            this.RefreshCommand.NotifyCanExecuteChanged();
        }
    }

    private bool CanExecuteRefresh() => !this.IsRefreshing;
}
