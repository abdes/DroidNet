// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
/// A layout strategy for the workspace.
/// </summary>
public sealed partial class DockingWorkspaceLayout : ObservableObject, IDisposable
{
    private const double LayoutSyncThrottleInMs = 150.0;

    private readonly IDisposable routerEventsSub;
    private readonly IRouter router;
    private readonly ILogger logger;
    private IDisposable? layoutChangedSub;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockingWorkspaceLayout"/> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="router">The router instance used for navigation and event subscription.</param>
    /// <param name="docker">The docker instance used for managing dockable layouts.</param>
    /// <param name="dockViewFactory">The factory used to create views for docks.</param>
    /// <param name="loggerFactory">Optional factory for creating loggers. If provided, enables detailed logging of the recognition process. If <see langword="null"/>, logging is disabled. </param>
    public DockingWorkspaceLayout(
        HostingContext hostingContext,
        IRouter router,
        IDocker docker,
        IDockViewFactory dockViewFactory,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger("WorkSpace") ?? NullLoggerFactory.Instance.CreateLogger("WorkSpace");

        this.router = router;

        var layout = new GridFlowLayout(dockViewFactory);

        this.routerEventsSub = router.Events.Subscribe(HandleRouterEvent);
        return;

        void HandleRouterEvent(RouterEvent routerEvent)
        {
            switch (routerEvent)
            {
                // At the beginning of each navigation cycle, many changes will happen to the
                // docker model, but we want to batch them together and handle them at once
                // after the navigation completes. So we mute the LayoutChanged event from the
                // docker.
                case NavigationStart:
                    this.layoutChangedSub?.Dispose();
                    break;

                // At the end of the navigation cycle, we need to take into account changes that
                // resulted from the new URL. We also resume listening to docker layout changes
                // that are not the result of navigation.
                case NavigationEnd:
                case NavigationError:
                    var options = ((NavigationEvent)routerEvent).Options;
                    if (options.AdditionalInfo is not AdditionalInfo { RebuildLayout: false })
                    {
                        docker.Layout(layout);
                        this.Content = layout.CurrentGrid;
                    }
                    else
                    {
                        Debug.WriteLine($"Not updating workspace because {nameof(AdditionalInfo.RebuildLayout)} is false");
                    }

                    // Layout changes can happen in a burst because a change to one panel can trigger changes in
                    // other panels. We throttle the burst and only sync with router after the burst is finished.
                    this.layoutChangedSub = Observable
                        .FromEventPattern<EventHandler<LayoutChangedEventArgs>, LayoutChangedEventArgs>(
                            h => docker.LayoutChanged += h,
                            h => docker.LayoutChanged -= h)
                        .Throttle(TimeSpan.FromMilliseconds(LayoutSyncThrottleInMs))
                        .Select(
                            layoutChangeEvent => Observable.FromAsync(
                                async () =>
                                {
                                    Debug.WriteLine($"Workspace layout changed because {layoutChangeEvent.EventArgs.Reason}");
                                    await hostingContext.Dispatcher.DispatchAsync(() => this.SyncWithRouterAsync(layoutChangeEvent.EventArgs.Reason)).ConfigureAwait(true); // Replaced SafeEnqueue with DispatchAsync
                                }))
                        .Concat()
                        .Subscribe();
                    break;

                // ReSharper disable once RedundantEmptySwitchSection
                default: // Ignore
                    break;
            }
        }
    }

    /// <summary>
    ///     Gets or sets the content of the workspace layout.
    /// </summary>
    [ObservableProperty]
    public partial UIElement Content { get; set; } = new Grid();

    /// <summary>
    /// Gets or sets the active route for the workspace layout.
    /// </summary>
    /// <value>The active route, or <see langword="null"/> if no route is active.</value>
    public IActiveRoute? ActiveRoute { get; set; }

    /// <inheritdoc/>
    public void Dispose() => this.routerEventsSub.Dispose();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Syncing docker workspace layout with the router...")]
    private static partial void LogSyncWithRouter(ILogger logger);

    private static bool CheckAndSetMinimized(DockingState dockState, IParameters active, Parameters next)
    {
        var minimized = dockState == DockingState.Minimized;
        return CheckAndSetFlag("minimized", minimized, active, next);
    }

    private static bool CheckAndSetAnchor(Anchor anchor, IParameters active, Parameters next)
    {
#pragma warning disable CA1308 // Normalize strings to uppercase
        // we use lowercase for the position
        var position = anchor.Position.ToString().ToLowerInvariant();
#pragma warning restore CA1308 // Normalize strings to uppercase
        next.AddOrUpdate(position, anchor.RelativeTo?.Id);

        // Trigger a change if the active parameters have the same anchor key as the one we are setting next with a different
        // value, or if it has any other anchor key than the one we are setting next.
        return active.ParameterHasValue(position, anchor.RelativeTo?.Id) ||
               Enum.GetNames<AnchorPosition>()
                   .Any(n => !string.Equals(n, position, StringComparison.OrdinalIgnoreCase) && active.Contains(n));
    }

    private static bool CheckAndSetWidth(string value, IParameters active, Parameters next) =>
        CheckAndSetParameterWithValue("w", value, active, next);

    private static bool CheckAndSetHeight(string value, IParameters active, Parameters next) =>
        CheckAndSetParameterWithValue("h", value, active, next);

    /// <summary>
    /// Checks if a parameter with value is changing from the currently active set of parameters to next set of parameters and
    /// sets the new value in next set of parameters.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The new value of the parameter.</param>
    /// <param name="active">The currently active parameter set.</param>
    /// <param name="next">The next set of parameters.</param>
    /// <returns>True if the parameter has changed, otherwise false.</returns>
    private static bool CheckAndSetParameterWithValue(string name, string? value, IParameters active, Parameters next)
    {
        next.AddOrUpdate(name, value);
        return active.ParameterHasValue(name, value);
    }

    /// <summary>
    /// Checks if a flag parameter is changing from the currently active set of parameters to next set of parameters and sets the
    /// flag in the next set of parameters according to the specified value.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The new value of the parameter.</param>
    /// <param name="active">The currently active parameter set.</param>
    /// <param name="next">The next set of parameters.</param>
    /// <returns>True if the parameter has changed, otherwise false.</returns>
    private static bool CheckAndSetFlag(string name, bool value, IParameters active, Parameters next)
    {
        next.SetFlag(name, value);
        return active.FlagIsSet(name) != value;
    }

    /// <summary>
    /// Generates a dictionary of parameters based on the provided dock's state and anchor properties. It also checks if any
    /// parameter has changed from the currently active parameters.
    /// </summary>
    /// <param name="dock">The dock instance whose properties are to be used for generating parameters.</param>
    /// <param name="dockable">The dockable for which parameters are to be generated.</param>
    /// <param name="activeParams">The currently active parameters for comparison. Default is null.</param>
    /// <returns>A dictionary of parameters if any parameter has changed or active parameters are null, otherwise null.</returns>
    private static Parameters? Parameters(
        IDock dock,
        IDockable dockable,
        IParameters? activeParams = null)
    {
        activeParams ??= ReadOnlyParameters.Empty;
        var changed = false;
        var nextParams = new Parameters();

        if (dock.State is DockingState.Minimized or DockingState.Pinned)
        {
            changed = CheckAndSetMinimized(dock.State, activeParams, nextParams);
        }

        if (dock.Anchor != null)
        {
            // The active dockable will get the dock's anchor, but any other
            // dockables will be anchored 'with' the active dockable.
            if (dockable.IsActive)
            {
                changed = CheckAndSetAnchor(dock.Anchor, activeParams, nextParams) || changed;
            }
            else
            {
                var anchor = new Anchor(AnchorPosition.With, dock.ActiveDockable);
                changed = CheckAndSetAnchor(anchor, activeParams, nextParams) || changed;
                anchor.Dispose();
            }
        }

        string? width = dockable.PreferredWidth;
        if (width != null)
        {
            changed = CheckAndSetWidth(width, activeParams, nextParams) || changed;
        }

        string? height = dockable.PreferredHeight;
        if (height != null)
        {
            changed = CheckAndSetHeight(height, activeParams, nextParams) || changed;
        }

        return changed ? nextParams : null;
    }

    private async Task SyncWithRouterAsync(LayoutChangeReason reason)
    {
        LogSyncWithRouter(this.logger);

        // Build a change set to manipulate the URL tree for our active route
        var changes = new List<RouteChangeItem>();

        // Delete closed docks
        this.TrackClosedDocks(changes);

        // Add and Update dockables from the managed dockables collection
        this.TrackChangedAndAddedDocks(changes);

        // Once the change set is built, request a partial navigation using it. If the layout change reason is anything but
        // Resize, we need to trigger a layout rebuild by setting the AdditionalInfo accordingly.
        await this.router.NavigateAsync(
            changes,
            new PartialNavigation()
            {
                RelativeTo = this.ActiveRoute,
                AdditionalInfo = new AdditionalInfo(reason != LayoutChangeReason.Resize),
            }).ConfigureAwait(true);
    }

    private void TrackChangedAndAddedDocks(List<RouteChangeItem> changes)
    {
        Debug.Assert(this.ActiveRoute is not null, "expecting to have an active route");

        foreach (var dockable in Dockable.All)
        {
            var dock = dockable.Owner;
            Debug.Assert(
                dock is not null,
                $"expecting a docked dockable, but dockable with id=`{dockable.Id}` has a null owner");

            /*
             * Our active route is the one for the dock workspace. For each dockable we have, there should be a
             * corresponding child active route with the outlet name being the same as the dockable id. If no child
             * active route with the same outlet then the dockable id is found, then a new one needs to be created for
             * this dockable. Otherwise, we just need to update the existing one.
             */

            var childRoute
                = this.ActiveRoute.Children.FirstOrDefault(r => dockable.Id.Equals(r.Outlet, StringComparison.Ordinal));
            if (childRoute is null)
            {
                Debug.Assert(
                    dockable.ViewModel is not null,
                    $"expecting a dockable to have a ViewModel, but dockable with id=`{dockable.Id}` does not");

                // A new active route needs to be added.
                changes.Add(
                    new RouteChangeItem()
                    {
                        ChangeAction = RouteChangeAction.Add,
                        Outlet = dockable.Id,
                        ViewModelType = dockable.ViewModel?.GetType(),
                        Parameters = Parameters(dock, dockable),
                    });
            }
            else
            {
                // Just update the existing active route.
                var parameters = Parameters(dock, dockable, childRoute.Params);
                if (parameters != null)
                {
                    changes.Add(
                        new RouteChangeItem()
                        {
                            ChangeAction = RouteChangeAction.Update,
                            Outlet = dockable.Id,
                            Parameters = parameters,
                        });
                }
            }
        }
    }

    private void TrackClosedDocks(List<RouteChangeItem> changes)
    {
        Debug.Assert(this.ActiveRoute is not null, "expecting to have an active route");

        changes.AddRange(
            from dockableRoute in this.ActiveRoute.Children
            where Dockable.FromId(dockableRoute.Outlet) == null
            select new RouteChangeItem()
            {
                ChangeAction = RouteChangeAction.Delete,
                Outlet = dockableRoute.Outlet,
            });
    }

    private record struct AdditionalInfo(bool RebuildLayout);
}
