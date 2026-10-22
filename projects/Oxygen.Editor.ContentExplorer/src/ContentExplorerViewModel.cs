// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Composition;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Routing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
///     The ViewModel for the <see cref="ContentExplorerView" /> view.
/// </summary>
/// <remarks>
///     This is an <see cref="IOutletContainer" /> with two outlets, the "left" outlet for the left pane, and the "right"
///     outlet for
///     the right pane.
/// </remarks>
[RetainedViewModel]
public sealed partial class ContentExplorerViewModel(
    ScopeFactory scopes,
    ScopeHandle window,
    IRouter parentRouter,
    IProjectSession session,
    IProjectUsageService projectUsage,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    ILoggerFactory? loggerFactory = null) : AbstractOutletContainer, IRoutingAware
{
    private const string DefaultLocalUrl = "/(left:folders//right:assets/tiles)";

    private static readonly Routes RoutesConfig = new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            Children = new Routes(
            [
                new Route { Outlet = "left", Path = "folders", ViewModelType = typeof(ContentFoldersViewModel) },
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
                        new Route
                        {
                            Path = "assets/details", Outlet = "right", ViewModelType = typeof(DetailsLayoutViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    private static readonly AssetPath ScenesFolder = AssetPath.Content.Combine("Scenes");

    private static readonly string[] AssetsViewPaths = ["assets/tiles", "assets/list", "assets/details"];

    private readonly ILogger logger = loggerFactory?.CreateLogger<ContentExplorerViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ContentExplorerViewModel>();

    // Navigation history management
    private readonly List<string> navigationHistory = [];

    private ContentExplorerAssetReveal? assetReveal;
    private ContentExplorerScriptCreation? scriptCreation;
    private Authoring.AuthoredAssetCreation? creation;
    private ScopeHandle? scope;
    private string currentAssetsViewPath = "assets/tiles"; // tracks whether we show list or tiles
    private int currentHistoryIndex = -1;
    private DispatcherQueue? dispatcher;

    private bool isDisposed;
    private bool isInitialized;
    private bool isNavigatingFromHistory;
    private bool isApplyingRouteState;

    private IRouter? localRouter;
    private IDisposable? routerEventsSubscription;
    private ContentExplorerState? browserState;

    /// <summary>
    ///     Gets a value indicating whether a refresh is currently in progress.
    ///     Used to disable refresh command during execution.
    /// </summary>
    [ObservableProperty]
    public partial bool IsRefreshing { get; set; }

    /// <summary>Gets the session search and filters shared with every asset layout.</summary>
    public AssetBrowserQuery Query { get; } = new();

    /// <summary>Gets the session result order, view, tile size and details-pane state.</summary>
    public AssetBrowserPresentation Presentation { get; } = new();

    /// <summary>Gets the details pane for the selected assets; available once the browser is initialized.</summary>
    [ObservableProperty]
    public partial AssetDetailsViewModel? Details { get; private set; }

    /// <summary>Gets the footer summary, for example "2 of 6 assets · 1 selected".</summary>
    [ObservableProperty]
    public partial string ResultSummary { get; private set; } = string.Empty;

    /// <summary>Gets whether the current location holds authored or read-only content.</summary>
    [ObservableProperty]
    public partial string SourceDescription { get; private set; } = string.Empty;

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
        this.assetReveal = window.Resolver.Resolve<ContentExplorerAssetReveal>(IfUnresolved.ReturnDefault);
        this.assetReveal?.Attach(this);

        // One-time initialization for singleton
        if (!this.isInitialized)
        {
            this.Outlets.Add("left", (nameof(this.LeftPaneViewModel), null));
            this.Outlets.Add("right", (nameof(this.RightPaneViewModel), null));

            this.scope = scopes.Create<IPanelLayer>(
                window,
                [
                    new RouterScopeModule(
                        RoutesConfig,
                        new LocalRouterContext(navigationContext.NavigationTarget) { ParentRouter = parentRouter, RootViewModel = this }),
                    new ContentExplorerPanelModule(),
                ],
                registrations => registrations.RegisterInstance(new ContentExplorerState { Query = this.Query, Presentation = this.Presentation }));
            var panel = this.scope.Resolver;

            // Make sure to use our own ViewModel to View converter
            this.VmToViewConverter = panel.Resolve<ViewModelToView>();

            // The window's New Script requests are answered by this browser's creation.
            this.creation = panel.Resolve<Authoring.AuthoredAssetCreation>();
            this.scriptCreation = window.Resolver.Resolve<ContentExplorerScriptCreation>(IfUnresolved.ReturnDefault);
            this.scriptCreation?.Attach(this.creation);

            this.localRouter = panel.Resolve<IRouter>();

            // Capture the UI Dispatcher from the hosting context to marshal UI-bound updates
            var hostingContext = panel.Resolve<HostingContext>();
            this.dispatcher = hostingContext.Dispatcher;

            // Subscribe to router events to track navigation history
            this.routerEventsSubscription = this.localRouter.Events
                .OfType<NavigationEnd>()
                .Subscribe(this.OnNavigationEnd);

            // Subscribe to ContentExplorerState changes to update the navigation stack
            var contentExplorerState = panel.Resolve<ContentExplorerState>();
            this.browserState = contentExplorerState;
            contentExplorerState.PropertyChanged += this.OnContentExplorerStateChanged;
            this.Details = panel.Resolve<AssetDetailsViewModel>();

            var initialUrl = await this.GetInitialContentExplorerUrlAsync().ConfigureAwait(true);
            await this.localRouter.NavigateAsync(initialUrl).ConfigureAwait(true);

            this.isInitialized = true;

            this.UpdateUpButtonState();
            this.UpdateBreadcrumbs();
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
        await this.localRouter.NavigateAsync(this.BuildUrl(target.Folder)).ConfigureAwait(true);
    }

    /// <summary>Describes whether a location holds authored content or read-only derived and mounted content.</summary>
    /// <param name="project">The active project.</param>
    /// <param name="folder">The current folder; <see langword="null"/> for the whole project.</param>
    /// <returns>The footer description.</returns>
    internal static string DescribeSource(ProjectContext? project, AssetPath? folder)
    {
        if (folder is null)
        {
            return "All project sources";
        }

        var authoring = project?.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.Name, folder.Mount, StringComparison.Ordinal));
        return authoring is not null && !ContentFoldersViewModel.IsPersistedProjectRelativeVirtualMount(authoring)
            ? "Authoring source" : "Read-only source";
    }

    /// <summary>Builds the footer summary from the active layout's counts and selection.</summary>
    /// <param name="resultCount">The results shown after search and filters.</param>
    /// <param name="scopeCount">The assets in the current folders.</param>
    /// <param name="selectedCount">The selected assets.</param>
    /// <returns>The summary, for example "2 of 6 assets · 1 selected".</returns>
    internal static string BuildResultSummary(int resultCount, int scopeCount, int selectedCount)
    {
        var culture = System.Globalization.CultureInfo.CurrentCulture;
        var results = resultCount == scopeCount
            ? string.Create(culture, $"{scopeCount} {(scopeCount == 1 ? "asset" : "assets")}")
            : string.Create(culture, $"{resultCount} of {scopeCount} assets");
        return selectedCount == 0 ? results : string.Create(culture, $"{results} · {selectedCount} selected");
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
            this.assetReveal?.Detach(this);
            if (this.creation is { } current)
            {
                this.scriptCreation?.Detach(current);
            }

            this.routerEventsSubscription?.Dispose();

            this.browserState?.PropertyChanged -= this.OnContentExplorerStateChanged;
            this.Details?.Dispose();

            // The explorer's scope has no async disposables to await: it completes before this returns.
            _ = this.scope?.DisposeAsync().AsTask();
        }

        base.Dispose(disposing);
    }

    // The folder the browser shows first when several are selected: the first in ordinal order.
    private static AssetPath? GetPrimarySelectedFolder(ContentExplorerState? state)
        => state?.SelectedFolders.Order().FirstOrDefault();

    private static bool TryNormalizeContentExplorerUrl(string persisted, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out string? url)
    {
        url = null;
        var trimmed = persisted.Trim();
        var assetsViewPath = AssetsViewPaths.FirstOrDefault(path => trimmed.Contains(path, StringComparison.Ordinal));

        if (assetsViewPath is null)
        {
            return false;
        }

        var query = ExtractSelectedQuery(trimmed);
        if (query is null && trimmed.Contains('?', StringComparison.Ordinal))
        {
            return false;
        }

        url = "/(left:folders//right:" + assetsViewPath + ")" + (query ?? string.Empty);
        return true;
        static string? ExtractSelectedQuery(string contentExplorerState)
        {
            var queryIndex = contentExplorerState.IndexOf('?', StringComparison.Ordinal);
            if (queryIndex < 0 || queryIndex == contentExplorerState.Length - 1)
            {
                return null;
            }

            var pairs = contentExplorerState[(queryIndex + 1)..].Split('&', StringSplitOptions.RemoveEmptyEntries);
            if (pairs.Length == 0)
            {
                return null;
            }

            var selected = new List<AssetPath>();
            foreach (var pair in pairs)
            {
                var parts = pair.Split('=', 2);
                if (parts.Length != 2 || !string.Equals(parts[0], RouteStateMapping.SelectedQueryKey, StringComparison.Ordinal))
                {
                    continue;
                }

                try
                {
                    if (AssetPath.TryParse(Uri.UnescapeDataString(parts[1]), out var folder))
                    {
                        selected.Add(folder);
                    }
                }
                catch (UriFormatException)
                {
                    return null;
                }
            }

            return selected.Count == 0
                ? null
                : RouteStateMapping.BuildSelectedQuery(selected);
        }
    }

    private static AssetBrowserView GetView(string assetsViewPath) => assetsViewPath switch
    {
        "assets/list" => AssetBrowserView.List,
        "assets/details" => AssetBrowserView.Details,
        _ => AssetBrowserView.Tiles,
    };

    // A new project's scenes are in Content/Scenes, which the browser opens on when it has no saved state.
    private static bool HasScenesFolder(ProjectContext project)
        => project.Find(ScenesFolder) is { } folder && project.FileSystem.Directory.Exists(folder.File.FullPath);

    private void OnContentExplorerStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(ContentExplorerState.ResultCount) or nameof(ContentExplorerState.SelectedAssets)
            && this.browserState is { } counted)
        {
            this.ResultSummary = BuildResultSummary(counted.ResultCount, counted.ScopeCount, counted.SelectedAssets.Count);
        }

        if (string.Equals(e.PropertyName, nameof(ContentExplorerState.SelectedFolders), StringComparison.Ordinal)
            && !this.isNavigatingFromHistory && !this.isApplyingRouteState)
        {
            // Build the new URL for history tracking but DON'T navigate
            var currentUrl = this.BuildCurrentUrl();

            this.LogContentExplorerStateChanged(currentUrl);

            // Only update history, don't trigger router navigation
            this.UpdateHistoryForStateChange(currentUrl);

            // Update the Up button state based on selection
            this.UpdateUpButtonState();

            // Update breadcrumbs
            this.UpdateBreadcrumbs();

            _ = this.PersistContentExplorerStateAsync(currentUrl);
        }
    }

    private async Task PersistContentExplorerStateAsync(string currentUrl)
    {
        var project = session.Context;

        try
        {
            await projectUsage.UpdateContentExplorerStateAsync(project.Name, project.ProjectRoot.FullPath, currentUrl).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogContentExplorerStatePersistFailed(ex, project.Name);
        }
    }

    private async Task<string> GetInitialContentExplorerUrlAsync()
    {
        var project = session.Context;
        var scenes = RouteStateMapping.BuildSelectedQuery(HasScenesFolder(project) ? [ScenesFolder] : []);
        var defaultUrl = DefaultLocalUrl + scenes;

        try
        {
            var usage = await projectUsage.GetProjectUsageAsync(project.Name, project.ProjectRoot.FullPath).ConfigureAwait(true);
            if (string.IsNullOrWhiteSpace(usage?.ContentExplorerState))
            {
                return defaultUrl;
            }

            if (TryNormalizeContentExplorerUrl(usage.ContentExplorerState, out var restoredUrl))
            {
                return restoredUrl.Contains('?', StringComparison.Ordinal) ? restoredUrl
                    : restoredUrl + scenes;
            }

            this.PublishPartialRestoreResult(
                DiagnosticCodes.WorkspacePrefix + "CONTENT_EXPLORER_STATE_INVALID",
                "The saved Content Explorer state could not be restored.",
                usage.ContentExplorerState);
            return defaultUrl;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishPartialRestoreResult(
                DiagnosticCodes.WorkspacePrefix + "CONTENT_EXPLORER_STATE_LOAD_FAILED",
                "The saved Content Explorer state could not be loaded.",
                project.ProjectRoot.FullPath,
                ex);
            return defaultUrl;
        }
    }

    private void PublishPartialRestoreResult(
        string code,
        string message,
        string? affectedPath,
        Exception? exception = null)
    {
        var operationId = Guid.NewGuid();
        var project = session.Context;
        var diagnostics = new List<DiagnosticRecord>
        {
            new()
            {
                OperationId = operationId,
                Domain = FailureDomain.WorkspaceRestoration,
                Severity = DiagnosticSeverity.Warning,
                Code = code,
                Message = message,
                TechnicalMessage = exception?.Message,
                ExceptionType = exception?.GetType().FullName,
                AffectedPath = affectedPath,
            },
        };

        var result = new OperationResult
        {
            OperationId = operationId,
            OperationKind = "Workspace.Restore",
            Status = statusReducer.Reduce(primaryGoalCompleted: true, wasCancelled: false, diagnostics),
            Severity = statusReducer.ComputeSeverity(diagnostics),
            Title = "Workspace partially restored",
            Message = "The workspace opened, but saved Content Explorer state was ignored.",
            StartedAt = DateTimeOffset.Now,
            CompletedAt = DateTimeOffset.Now,
            AffectedScope = new AffectedScope
            {
                ProjectId = project.ProjectId,
                ProjectName = project.Name,
                ProjectPath = project.ProjectRoot.FullPath,
            },
            Diagnostics = diagnostics,
            PrimaryAction = new PrimaryAction
            {
                ActionId = "open-details",
                Label = "Details",
                Kind = PrimaryActionKind.OpenDetails,
            },
        };

        operationResults.Publish(result);
    }

    private string BuildCurrentUrl()
        => "/(left:folders//right:" + this.currentAssetsViewPath + ")"
            + RouteStateMapping.BuildSelectedQuery(this.scope?.Resolver.Resolve<ContentExplorerState>()?.SelectedFolders ?? (IEnumerable<AssetPath>)[]);

    // A URL that shows one folder, or the whole project when there is none.
    private string BuildUrl(AssetPath? folder)
        => "/(left:folders//right:" + this.currentAssetsViewPath + ")" + RouteStateMapping.BuildSelectedQuery(folder is null ? [] : [folder]);

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

        // Track the current results view from the URL.
        var url = navigationEnd.Url ?? string.Empty;
        if (AssetsViewPaths.FirstOrDefault(path => url.Contains(path, StringComparison.Ordinal)) is { } viewPath)
        {
            this.currentAssetsViewPath = viewPath;
        }

        this.Presentation.View = GetView(this.currentAssetsViewPath);

        this.isApplyingRouteState = true;
        try
        {
            if (this.scope?.Resolver.Resolve<ContentExplorerState>() is { } state)
            {
                RouteStateMapping.ApplyNavigationScope(state, url);
            }
        }
        finally
        {
            this.isApplyingRouteState = false;
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

        this.UpdateUpButtonState();
        this.UpdateBreadcrumbs();
        _ = this.PersistContentExplorerStateAsync(this.BuildCurrentUrl());
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

        // The whole project has no parent; a mount's parent is the whole project.
        if (GetPrimarySelectedFolder(this.scope?.Resolver.Resolve<ContentExplorerState>()) is not { } current)
        {
            return;
        }

        var url = this.BuildUrl(current.Parent);
        this.LogNavigationUp(url, current);
        await this.localRouter.NavigateAsync(url).ConfigureAwait(true);
    }

    private void UpdateUpButtonState()
    {
        var current = GetPrimarySelectedFolder(this.scope?.Resolver.Resolve<ContentExplorerState>());

        var old = this.CanGoUp;
        this.CanGoUp = current is not null;
        if (old != this.CanGoUp)
        {
            this.LogCanGoUpUpdated(this.CanGoUp, current, current?.Parent);
        }

        this.GoUpCommand.NotifyCanExecuteChanged();
    }

    private void UpdateBreadcrumbs(AssetPath? pathOverride = null)
    {
        // Build the new list fully, then swap the collection on the UI thread to avoid per-item change notifications
        void UpdateCore()
        {
            var entries = new List<BreadcrumbEntry>();

            var primary = pathOverride;
            if (primary is null)
            {
                var state = this.scope?.Resolver.Resolve<ContentExplorerState>();
                primary = GetPrimarySelectedFolder(state);
            }

            // Determine project name for root label if available
            var rootLabel = "Project";
            try
            {
                var pn = this.scope?.Resolver.Resolve<IProjectSession>().Context.Name;
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
            entries.Add(new BreadcrumbEntry(rootLabel, Folder: null));
            this.SourceDescription = DescribeSource(session.Context, primary);

            // The folder's mount, then each folder down to it.
            var crumbs = new Stack<AssetPath>();
            for (var folder = primary; folder is not null; folder = folder.Parent)
            {
                crumbs.Push(folder);
            }

            entries.AddRange(crumbs.Select(static folder => new BreadcrumbEntry(folder.Name, folder)));

            // Swap the entire collection to minimize WinRT collection change handling issues
            this.Breadcrumbs = [.. entries];
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

    /// <summary>Shows results as preview tiles.</summary>
    [RelayCommand]
    private Task SwitchToTilesViewAsync() => this.SwitchViewAsync("assets/tiles");

    /// <summary>Shows results as a compact list.</summary>
    [RelayCommand]
    private Task SwitchToListViewAsync() => this.SwitchViewAsync("assets/list");

    /// <summary>Shows results as a table with sortable columns.</summary>
    [RelayCommand]
    private Task SwitchToDetailsViewAsync() => this.SwitchViewAsync("assets/details");

    private async Task SwitchViewAsync(string assetsViewPath)
    {
        if (this.localRouter is null)
        {
            return;
        }

        this.currentAssetsViewPath = assetsViewPath;
        this.Presentation.View = GetView(assetsViewPath);
        await this.localRouter.NavigateAsync(this.BuildCurrentUrl()).ConfigureAwait(true);
    }

    /// <summary>
    ///     Forces re-indexing of assets for the currently selected folders.
    /// </summary>
    [RelayCommand(CanExecute = nameof(CanExecuteRefresh))]
    private async Task RefreshAsync()
    {
        if (this.scope is null)
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

            if (this.LeftPaneViewModel is ContentFoldersViewModel projectLayout)
            {
                await projectLayout.RefreshTreeAsync().ConfigureAwait(true);
            }

            await this.scope.Resolver.Resolve<IAssetFeed>().RefreshAsync().ConfigureAwait(true);
        }
        finally
        {
            this.IsRefreshing = false;
            this.RefreshCommand.NotifyCanExecuteChanged();
        }
    }

    private bool CanExecuteRefresh() => !this.IsRefreshing;
}
