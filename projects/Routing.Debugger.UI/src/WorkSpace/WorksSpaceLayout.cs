// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>A layout strategy for the workspace.</summary>
public sealed partial class WorkSpaceLayout : ObservableObject, IDisposable
{
    private readonly IDisposable routerEventsSub;
    private readonly IRouter router;
    private readonly ILogger logger;

    private IActiveRoute? activeRoute;

    [ObservableProperty]
    private UIElement content = new Grid();

    public WorkSpaceLayout(IRouter router, IDocker docker, IDockViewFactory dockViewFactory, ILogger logger)
    {
        this.router = router;
        this.logger = logger;

        var layout = new GridFlowLayout(dockViewFactory);

        this.routerEventsSub = router.Events.Subscribe(
            @event =>
            {
                switch (@event)
                {
                    // At the beginning of each navigation cycle, many changes
                    // will happen to ro docker model, but we want to batch them
                    // together and handle them at once after the navigation
                    // completes. So we mute the LayoutChanged event from the
                    // docker.
                    case NavigationStart:
                        docker.LayoutChanged -= this.SyncWithRouter;
                        break;

                    case ActivationComplete activationComplete:
                        this.activeRoute = FindWorkSpaceActiveRoute(activationComplete.RouterState.Root);
                        break;

                    // At the end of the navigation cycle, we need to take into
                    // account changes that resulted from the new URL. We also
                    // resume listening to docker layout changes that are not
                    // the result of navigation.
                    case NavigationEnd:
                    case NavigationError:
                        var options = ((NavigationEvent)@event).Options;
                        if (options.AdditionalInfo is not AdditionalInfo { RebuildLayout: false })
                        {
                            this.Content = layout.Build(docker.Root);
                        }
                        else
                        {
                            Debug.WriteLine(
                                $"Not updating workspace because {nameof(AdditionalInfo.RebuildLayout)} is false");
                        }

                        docker.LayoutChanged += this.SyncWithRouter;
                        break;

                    default: // Ignore
                        break;
                }
            });
    }

    public void Dispose() => this.routerEventsSub.Dispose();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Syncing docker workspace layout with the router...")]
    private static partial void LogSyncWithRouter(ILogger logger);

    private static IActiveRoute? FindWorkSpaceActiveRoute(IActiveRoute? node)
    {
        if (node is null)
        {
            return null;
        }

        return node.Outlet == DebuggerConstants.DockOutletName
            ? node
            : node.Children.Select(FindWorkSpaceActiveRoute).FirstOrDefault();
    }

    private static bool CheckAndSetMinimized(DockingState dockState, IParameters active, Parameters next)
    {
        var minimized = dockState == DockingState.Minimized;
        return CheckAndSetFlag("minimized", minimized, active, next);
    }

    private static bool CheckAndSetAnchor(Anchor anchor, IParameters active, Parameters next)
    {
        var position = anchor.Position.ToString().ToLowerInvariant();
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
            var anchor = dockable.IsActive
                ? dock.Anchor
                : new Anchor(AnchorPosition.With, dock.ActiveDockable);
            changed = CheckAndSetAnchor(anchor, activeParams, nextParams) || changed;
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

    private void SyncWithRouter(object? sender, LayoutChangedEventArgs args)
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
        this.router.Navigate(
            changes,
            new PartialNavigation()
            {
                RelativeTo = this.activeRoute,
                AdditionalInfo = new AdditionalInfo(args.Reason != LayoutChangeReason.Resize),
            });
    }

    private void TrackChangedAndAddedDocks(List<RouteChangeItem> changes)
    {
        Debug.Assert(this.activeRoute is not null, "expecting to have an active route");

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
                = this.activeRoute.Children.FirstOrDefault(r => dockable.Id.Equals(r.Outlet, StringComparison.Ordinal));
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
        Debug.Assert(this.activeRoute is not null, "expecting to have an active route");

        changes.AddRange(
            from dockableRoute in this.activeRoute.Children
            where Dockable.FromId(dockableRoute.Outlet) == null
            select new RouteChangeItem()
            {
                ChangeAction = RouteChangeAction.Delete,
                Outlet = dockableRoute.Outlet,
            });
    }

    private record struct AdditionalInfo(bool RebuildLayout);
}
