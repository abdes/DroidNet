// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Oxygen.Editor.WorldEditor.Routing;
using IContainer = DryIoc.IContainer;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="ContentBrowserView"/> view.
/// </summary>
/// <remarks>
/// This is an <see cref="IOutletContainer"/> with two outlets, the "left" outlet for the left pane, and the "right" outlet for
/// the right pane.
/// </remarks>
public sealed partial class ContentBrowserViewModel(IContainer container, IRouter parentRouter, ILoggerFactory? loggerFactory = null) : AbstractOutletContainer, IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ContentBrowserViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ContentBrowserViewModel>();
    private static readonly Routes RoutesConfig = new(
    [
        new Route()
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            Children = new Routes(
            [
                new Route() { Outlet = "left", Path = "project", ViewModelType = typeof(ProjectLayoutViewModel), },
                new Route()
                {
                    Path = string.Empty,
                    Outlet = "right",
                    ViewModelType = typeof(AssetsViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "assets/tiles", Outlet = "right", ViewModelType = typeof(TilesLayoutViewModel),
                        },
                        new Route()
                        {
                            Path = "assets/list", Outlet = "right", ViewModelType = typeof(ListLayoutViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    private bool isDisposed;
    private IContainer? childContainer;
    private IRouter? localRouter;
    private IDisposable? routerEventsSubscription;
    private bool isInitialized;

    // Navigation history management
    private readonly List<string> navigationHistory = [];
    private int currentHistoryIndex = -1;
    private bool isNavigatingFromHistory = false;

    /// <summary>
    /// Gets the local ViewModel to View converter. Guaranteed to be not <see langword="null"/> when the view is loaded.
    /// </summary>
    public ViewModelToView? VmToViewConverter { get; private set; }

    /// <summary>
    /// Gets the ViewModel for the left pane.
    /// </summary>
    public object? LeftPaneViewModel => this.Outlets["left"].viewModel;

    /// <summary>
    /// Gets the ViewModel for the right pane.
    /// </summary>
    public object? RightPaneViewModel => this.Outlets["right"].viewModel;

    /// <summary>
    /// Gets a value indicating whether back navigation is possible.
    /// </summary>
    [ObservableProperty]
    private bool canGoBack;

    /// <summary>
    /// Gets a value indicating whether forward navigation is possible.
    /// </summary>
    [ObservableProperty]
    private bool canGoForward;

    /// <summary>
    /// Gets a value indicating whether navigating up is possible.
    /// </summary>
    [ObservableProperty]
    private bool canGoUp;

    /// <summary>
    /// Gets a value indicating whether a refresh is currently in progress.
    /// Used to disable refresh command during execution.
    /// </summary>
    [ObservableProperty]
    private bool isRefreshing;

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // One-time initialization for singleton
        if (!this.isInitialized)
        {
            this.Outlets.Add("left", (nameof(this.LeftPaneViewModel), null));
            this.Outlets.Add("right", (nameof(this.RightPaneViewModel), null));

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
            this.childContainer.Register<AssetsIndexingService>(Reuse.Singleton);

            this.childContainer.Register<ProjectLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<ProjectLayoutView>(Reuse.Singleton);
            this.childContainer.Register<AssetsViewModel>(Reuse.Singleton);
            this.childContainer.Register<AssetsView>(Reuse.Singleton);
            this.childContainer.Register<ListLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<ListLayoutView>(Reuse.Singleton);
            this.childContainer.Register<TilesLayoutViewModel>(Reuse.Singleton);
            this.childContainer.Register<TilesLayoutView>(Reuse.Singleton);

            // Make sure to use our own ViewModel to View converter
            this.VmToViewConverter = this.childContainer.Resolve<ViewModelToView>();

            this.localRouter = this.childContainer.Resolve<IRouter>();

            // Subscribe to router events to track navigation history
            this.routerEventsSubscription = this.localRouter.Events
                .OfType<NavigationEnd>()
                .Subscribe(this.OnNavigationEnd);

            // Subscribe to ContentBrowserState changes to update the navigation stack
            var contentBrowserState = this.childContainer.Resolve<ContentBrowserState>();
            contentBrowserState.PropertyChanged += this.OnContentBrowserStateChanged;

            var initialUrl = "/(left:project//right:assets/list)";
            await this.localRouter.NavigateAsync(initialUrl).ConfigureAwait(true);
            // Do NOT add to history here; NavigationEnd handler will record it once

            this.isInitialized = true;

            // Initialize Up button state
            this.UpdateUpButtonState();
        }

        // Actions that should happen on every navigation (if any)
        // Currently none needed for ContentBrowserViewModel
    }

    private void OnContentBrowserStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ContentBrowserState.SelectedFolders) && !this.isNavigatingFromHistory)
        {
            // Build the new URL for history tracking but DON'T navigate
            var currentUrl = this.BuildCurrentUrl();

            Debug.WriteLine($"[ContentBrowserViewModel] ContentBrowserState changed, updating history only: {currentUrl}");

            // Only update history, don't trigger router navigation
            this.UpdateHistoryForStateChange(currentUrl);

            // Update the Up button state based on selection
            this.UpdateUpButtonState();
        }
    }

    private string BuildCurrentUrl()
    {
        var contentBrowserState = this.childContainer?.Resolve<ContentBrowserState>();
        if (contentBrowserState == null)
        {
            return "/(left:project//right:assets/list)";
        }

        if (contentBrowserState.SelectedFolders.Count == 0)
        {
            // No folders selected - return URL without query parameters
            return "/(left:project//right:assets/list)";
        }

        // Sort folders to ensure consistent URL generation
        var sortedFolders = contentBrowserState.SelectedFolders.OrderBy(f => f, StringComparer.Ordinal);
        var selectedParams = string.Join("&", sortedFolders.Select(folder => $"selected={Uri.EscapeDataString(folder)}"));
        return $"/(left:project//right:assets/list)?{selectedParams}";
    }

    private void UpdateHistoryForStateChange(string url)
    {
        Debug.WriteLine($"[NavigationHistory] UpdateHistoryForStateChange: URL={url}");

        // Only update history if this is a new URL
        if (this.navigationHistory.Count == 0 || this.navigationHistory[this.currentHistoryIndex] != url)
        {
            Debug.WriteLine($"[NavigationHistory] State change URL is new, adding to history");
            this.AddToHistory(url);
        }
        else
        {
            Debug.WriteLine($"[NavigationHistory] State change URL is same as current, not adding to history");
        }
    }

    private void OnNavigationEnd(NavigationEnd navigationEnd)
    {
        Debug.WriteLine($"[NavigationHistory] NavigationEnd event: URL={navigationEnd.Url}, IsFromHistory={this.isNavigatingFromHistory}");

        if (!this.isNavigatingFromHistory && !string.IsNullOrEmpty(navigationEnd.Url))
        {
            // Avoid pushing duplicate if it's identical to the current entry
            if (this.navigationHistory.Count == 0 || this.navigationHistory[this.currentHistoryIndex] != navigationEnd.Url)
            {
                Debug.WriteLine($"[NavigationHistory] Adding NavigationEnd URL to history");
                this.AddToHistory(navigationEnd.Url);
            }
            else
            {
                Debug.WriteLine($"[NavigationHistory] NavigationEnd URL matches current, not adding");
            }
        }
        else
        {
            Debug.WriteLine($"[NavigationHistory] Skipping NavigationEnd URL (from history or empty)");
        }

        this.UpdateHistoryButtonStates();
    }

    private void AddToHistory(string url)
    {
        Debug.WriteLine($"[NavigationHistory] Adding to history: {url}");

        // If the last entry equals the new URL, skip to prevent duplicates
        if (this.navigationHistory.Count > 0 && this.navigationHistory[this.currentHistoryIndex] == url)
        {
            Debug.WriteLine($"[NavigationHistory] Duplicate of current entry, skipping add");
            this.UpdateHistoryButtonStates();
            return;
        }

        // Remove any forward history if we're navigating to a new location
        if (this.currentHistoryIndex < this.navigationHistory.Count - 1)
        {
            var removedCount = this.navigationHistory.Count - this.currentHistoryIndex - 1;
            Debug.WriteLine($"[NavigationHistory] Removing {removedCount} forward history items from index {this.currentHistoryIndex + 1}");
            this.navigationHistory.RemoveRange(this.currentHistoryIndex + 1, removedCount);
        }

        // Add new URL to history
        this.navigationHistory.Add(url);
        this.currentHistoryIndex = this.navigationHistory.Count - 1;
    this.LogHistoryPush(url);

        Debug.WriteLine($"[NavigationHistory] History state: Count={this.navigationHistory.Count}, CurrentIndex={this.currentHistoryIndex}");
        Debug.WriteLine($"[NavigationHistory] Full history: [{string.Join(", ", this.navigationHistory.Select((u, i) => i == this.currentHistoryIndex ? $"*{u}*" : u))}]");

        this.UpdateHistoryButtonStates();
    }

    private void UpdateHistoryButtonStates()
    {
        var oldCanGoBack = this.CanGoBack;
        var oldCanGoForward = this.CanGoForward;

        this.CanGoBack = this.currentHistoryIndex > 0;
        this.CanGoForward = this.currentHistoryIndex < this.navigationHistory.Count - 1;

        Debug.WriteLine($"[NavigationHistory] Button states updated: CanGoBack={this.CanGoBack} (was {oldCanGoBack}), CanGoForward={this.CanGoForward} (was {oldCanGoForward})");
        Debug.WriteLine($"[NavigationHistory] Current state: Index={this.currentHistoryIndex}, Count={this.navigationHistory.Count}");

        // Notify the commands that their CanExecute state may have changed
        this.GoBackCommand.NotifyCanExecuteChanged();
        this.GoForwardCommand.NotifyCanExecuteChanged();

        // Up button depends on current selection, update it too
        this.UpdateUpButtonState();
    }

    /// <summary>
    /// Navigates back in the history.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoBack))]
    private async Task GoBackAsync()
    {
        if (this.CanGoBack && this.localRouter != null)
        {
            Debug.WriteLine($"[NavigationHistory] Going back from index {this.currentHistoryIndex} to {this.currentHistoryIndex - 1}");
            this.currentHistoryIndex--;
            var targetUrl = this.navigationHistory[this.currentHistoryIndex];
            Debug.WriteLine($"[NavigationHistory] Navigating back to: {targetUrl}");

            this.isNavigatingFromHistory = true;

            try
            {
                await this.localRouter.NavigateAsync(targetUrl).ConfigureAwait(true);
                this.LogHistoryPopBack(targetUrl);
            }
            finally
            {
                this.isNavigatingFromHistory = false;
                this.UpdateHistoryButtonStates();
            }
        }
    }

    /// <summary>
    /// Navigates forward in the history.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoForward))]
    private async Task GoForwardAsync()
    {
        if (this.CanGoForward && this.localRouter != null)
        {
            Debug.WriteLine($"[NavigationHistory] Going forward from index {this.currentHistoryIndex} to {this.currentHistoryIndex + 1}");
            this.currentHistoryIndex++;
            var targetUrl = this.navigationHistory[this.currentHistoryIndex];
            Debug.WriteLine($"[NavigationHistory] Navigating forward to: {targetUrl}");

            this.isNavigatingFromHistory = true;

            try
            {
                await this.localRouter.NavigateAsync(this.navigationHistory[this.currentHistoryIndex]).ConfigureAwait(true);
                this.LogHistoryPopForward(targetUrl);
            }
            finally
            {
                this.isNavigatingFromHistory = false;
                this.UpdateHistoryButtonStates();
            }
        }
    }

    /// <inheritdoc/>
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

    /// <summary>
    /// Navigates to the parent of the currently selected folder in the project tree.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanGoUp))]
    private async Task GoUpAsync()
    {
        if (this.localRouter is null)
        {
            return;
        }

        var state = this.childContainer?.Resolve<ContentBrowserState>();
        var current = this.GetPrimarySelectedFolder(state);

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

        var url = "/(left:project//right:assets/list)";
        if (parent.Length > 0)
        {
            url += $"?selected={Uri.EscapeDataString(parent)}";
        }
        else
        {
            // Parent is the project root; represent it explicitly as selected=.
            url += "?selected=.";
        }

        Debug.WriteLine($"[Navigation] GoUp to: {url} (from '{current}')");
        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    private void UpdateUpButtonState()
    {
        var state = this.childContainer?.Resolve<ContentBrowserState>();
        var current = this.GetPrimarySelectedFolder(state);
        var parent = current is not null ? GetParentRelativePath(current) : null;

        var old = this.CanGoUp;
        this.CanGoUp = parent is not null;
        if (old != this.CanGoUp)
        {
            Debug.WriteLine($"[Navigation] CanGoUp updated: {this.CanGoUp} (was {old}), current='{current}', parent='{parent}'");
        }

        this.GoUpCommand.NotifyCanExecuteChanged();
    }

    private string? GetPrimarySelectedFolder(ContentBrowserState? state)
    {
        if (state is null || state.SelectedFolders.Count == 0)
        {
            return null;
        }

        // Choose a deterministic primary folder when multiple are selected: the first in ordinal order
        return state.SelectedFolders.OrderBy(f => f, StringComparer.Ordinal).FirstOrDefault();
    }

    private static string? GetParentRelativePath(string? relativePath)
    {
        if (string.IsNullOrEmpty(relativePath) || relativePath == ".")
        {
            return null; // No parent above root
        }

        // Handle both Windows and URL style separators
        var idx = relativePath.LastIndexOfAny(new[] { '\\', '/' });
        if (idx < 0)
        {
            return string.Empty; // Parent is project root
        }

        if (idx == 0)
        {
            return string.Empty;
        }

        return relativePath.Substring(0, idx);
    }

    [LoggerMessage(
        EventName = $"ui-{nameof(ContentBrowserViewModel)}-HistoryPush",
        Level = LogLevel.Information,
        Message = "Pushed URL: `{Url}`")]
    private partial void LogHistoryPush(string url);

    [LoggerMessage(
        EventName = $"ui-{nameof(ContentBrowserViewModel)}-HistoryPopBack",
        Level = LogLevel.Information,
        Message = "Navigated Back to: `{Url}`")]
    private partial void LogHistoryPopBack(string url);

    [LoggerMessage(
        EventName = $"ui-{nameof(ContentBrowserViewModel)}-HistoryPopForward",
        Level = LogLevel.Information,
        Message = "Navigated Forward to: `{Url}`")]
    private partial void LogHistoryPopForward(string url);

    [LoggerMessage(
        EventName = $"ui-{nameof(ContentBrowserViewModel)}-RefreshRequested",
        Level = LogLevel.Information,
        Message = "Refresh requested for selected folders")]
    private partial void LogRefreshRequested();

    /// <summary>
    /// Forces re-indexing of assets for the currently selected folders.
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

            // Then refresh asset indexing
            var indexing = this.childContainer.Resolve<AssetsIndexingService>();
            await indexing.RefreshAssetsAsync().ConfigureAwait(true);
        }
        finally
        {
            this.IsRefreshing = false;
            this.RefreshCommand.NotifyCanExecuteChanged();
        }
    }

    private bool CanExecuteRefresh() => !this.IsRefreshing;
}
