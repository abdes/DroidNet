// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Routing.Debugger.UI.Docks;
using DroidNet.Routing.Events;
using DroidNet.Routing.View;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

/// <summary>
/// A layout strategy for the workspace.
/// </summary>
public sealed partial class WorkSpaceLayout : ObservableObject, IDisposable
{
    private readonly Stack<VectorGrid> grids = new();
    private readonly IDisposable routerEventsSub;
    private readonly IRouter router;
    private readonly IDocker docker;
    private readonly IViewLocator viewLocator;
    private readonly ILogger logger;

    private IActiveRoute? activeRoute;

    [ObservableProperty]
    private UIElement content = new Grid();

    public WorkSpaceLayout(IRouter router, IDocker docker, IViewLocator viewLocator, ILogger logger)
    {
        this.router = router;
        this.docker = docker;
        this.viewLocator = viewLocator;
        this.logger = logger;

        this.routerEventsSub = this.router.Events.Subscribe(
            @event =>
            {
                switch (@event)
                {
                    // At the beginning of each navigation cycle, many
                    // changes will happen to ro docker model, but we want
                    // to batch them together and handle them at once after
                    // the navigation completes. So we mute the
                    // LayoutChanged event from the docker.
                    case NavigationStart:
                        this.docker.LayoutChanged -= this.SyncWithRouter;
                        break;

                    case ActivationComplete activationComplete:
                        this.activeRoute = FindWorkSpaceActiveRoute(activationComplete.RouterState.Root);
                        break;

                    // At the end of the navigation cycle, we need to take
                    // into account changes that resulted from the new URL.
                    // We also resume listening to docker layout changes
                    // that are not the result of navigation.
                    case NavigationEnd:
                    case NavigationError:
                        var options = ((NavigationEvent)@event).Options;
                        if (options.AdditionalInfo is not AdditionalInfo { RebuildLayout: false })
                        {
                            this.Content = this.UpdateContent();
                        }
                        else
                        {
                            Debug.WriteLine(
                                $"Not updating workspace because {nameof(AdditionalInfo.RebuildLayout)} is false");
                        }

                        this.docker.LayoutChanged += this.SyncWithRouter;
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

    private static IActiveRoute? FindWorkSpaceActiveRoute(IActiveRoute? node) =>
        node is null
            ? null
            : node.Outlet == DebuggerConstants.DockOutletName
                ? node
                : node.Children.Select(FindWorkSpaceActiveRoute).FirstOrDefault();

    private static bool CheckAndSetMinimized(DockingState dockState, IParameters active, Parameters next)
    {
        var minimized = dockState == DockingState.Minimized;
        return CheckAndSetFlag("minimized", minimized, active, next);
    }

    private static bool CheckAndSetAnchor(Anchor anchor, IParameters active, Parameters next)
    {
        // TODO: if the active parameters has any anchor that is not the same then what we need to set next, it should trigger a change
        var position = anchor.Position.ToString().ToLowerInvariant();
        var relativeTo = anchor.DockId?.ToString();
        return CheckAndSetParameterWithValue(position, relativeTo, active, next);
    }

    private static bool CheckAndSetWidth(string value, IParameters active, Parameters next) =>
        CheckAndSetParameterWithValue("w", value, active, next);

    private static bool CheckAndSetHeight(string value, IParameters active, Parameters next) =>
        CheckAndSetParameterWithValue("h", value, active, next);

    /// <summary>
    /// Checks if a parameter with value is changing from the currently active
    /// set of parameters to next set of parameters and sets the new value in
    /// next set of parameters.
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
    /// Checks if a flag parameter is changing from the currently active set of
    /// parameters to next set of parameters and sets the flag in the next set
    /// of parameters according to the specified value.
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
    /// Generates a dictionary of parameters based on the provided dock's state
    /// and anchor properties. It also checks if any parameter has changed from
    /// the currently active parameters.
    /// </summary>
    /// <param name="dock">The dock instance whose properties are to be used for
    /// generating parameters.</param>
    /// <param name="activeParams">The currently active parameters for
    /// comparison. Default is null.</param>
    /// <returns>A dictionary of parameters if any parameter has changed or
    /// active parameters are null, otherwise null.</returns>
    private static Parameters? Parameters(
        IDock dock,
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
            changed = CheckAndSetAnchor(dock.Anchor, activeParams, nextParams) || changed;
        }

        // TODO: update this after dock with is supported
        var width = dock.Dockables.FirstOrDefault()?.PreferredWidth.Value;
        if (width != null)
        {
            changed = CheckAndSetWidth(width, activeParams, nextParams) || changed;
        }

        var height = dock.Dockables.FirstOrDefault()?.PreferredHeight.Value;
        if (height != null)
        {
            changed = CheckAndSetHeight(height, activeParams, nextParams) || changed;
        }

        return changed ? nextParams : null;
    }

    private VectorGrid UpdateContent()
    {
        var orientation = this.docker.Root.Orientation == DockGroupOrientation.Vertical
            ? Orientation.Vertical
            : Orientation.Horizontal;
        Debug.WriteLine($"Start a new {orientation} VG for the root dock");
        var grid = new VectorGrid(orientation);
        this.PushGrid(grid, "root");
        this.Layout(this.docker.Root);
        Debug.Assert(this.grids.Count == 1, "some pushes to the grids stack were not matched by a corresponding pop");
        _ = this.grids.Pop();

        return grid;
    }

    private void PushGrid(VectorGrid grid, string what)
    {
        Debug.WriteLine($"{(grid.Orientation == Orientation.Horizontal ? "---" : " | ")} Push grid {what}");
        this.grids.Push(grid);
    }

    private void PopGrid(string? what)
    {
        _ = this.grids.Pop();
        Debug.WriteLine($"{(this.grids.Peek().Orientation == Orientation.Horizontal ? "---" : " | ")} Pop grid {what}");
    }

    private void PlaceTray(IDockTray tray)
    {
        if (tray.IsEmpty)
        {
            return;
        }

        Debug.WriteLine($"Add tray {tray} to VG (fixed size)");
        var grid = this.grids.Peek();

        var trayOrientation = tray.IsVertical ? Orientation.Vertical : Orientation.Horizontal;
        var trayViewModel = new DockTrayViewModel(this.docker, tray, trayOrientation);
        var trayControl = new DockTray() { ViewModel = trayViewModel };
        grid.AddFixedSizeItem(trayControl, GridLength.Auto, 32);
    }

    private void Layout(IDockGroup group)
    {
        if (!group.ShouldShowGroup())
        {
            Debug.WriteLine($"Skipping {group}");
            return;
        }

        var stretch = group.ShouldStretch();

        var grid = this.grids.Peek();
        if (group.Orientation != DockGroupOrientation.Undetermined)
        {
            var orientation = group.Orientation == DockGroupOrientation.Vertical
                ? Orientation.Vertical
                : Orientation.Horizontal;
            if (grid.Orientation != orientation)
            {
                var newGrid = new VectorGrid(orientation);
                Debug.WriteLine($"Add new grid for group `{group} to VG");
                Debug.WriteLine($"Group {group.First} should {(stretch ? string.Empty : " NOT")}stretch");
                grid.AddResizableItem(
                    newGrid,
                    stretch ? new GridLength(1, GridUnitType.Star) : GridLength.Auto,
                    32);
                grid = newGrid;
            }
        }

        if (!group.IsEmpty)
        {
            // A group with docks -> Layout the docks as items in the vector grid.
            foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
            {
                Debug.WriteLine($"Add dock {dock} from group `{group} to VG");
                grid.AddResizableItem(
                    new Border()
                    {
                        Child = dock is ApplicationDock appDock
                            ? new EmbeddedAppView()
                            {
                                ViewModel = new EmbeddedAppViewModel(
                                    appDock.ApplicationViewModel,
                                    this.viewLocator,
                                    this.logger),
                            }
                            : new DockPanel()
                            {
                                ViewModel = new DockPanelViewModel(dock, this.docker),
                            },
                        BorderBrush = new SolidColorBrush(Colors.Red),
                        BorderThickness = new Thickness(0.5),
                    },
                    stretch ? new GridLength(1, GridUnitType.Star) : GetGridLengthForDock(dock, grid.Orientation),
                    32);
            }

            return;
        }

        // Handle the children
        this.PushGrid(grid, (grid == this.grids.Peek() ? "!!new " : "=same ") + group);
        HandlePart(group.First);
        HandlePart(group.Second);

        this.PopGrid(group.ToString());
        return;

        void HandlePart(IDockGroup? part)
        {
            switch (part)
            {
                case null:
                    return;

                case IDockTray tray:
                    this.PlaceTray(tray);
                    break;

                default:
                    this.Layout(part);
                    break;
            }
        }

        GridLength GetGridLengthForDock(IDock dock, Orientation gridOrientation)
        {
            var activeDockable = dock.Dockables.FirstOrDefault(); // TODO: change this after active dockable is tracked
            if (activeDockable == null)
            {
                return new GridLength(300);
            }

            return gridOrientation == Orientation.Horizontal
                ? activeDockable.PreferredGridWidth()
                : activeDockable.PreferredGridHeight();
        }
    }

    private void SyncWithRouter(LayoutChangeReason layoutChangeReason)
    {
        Debug.Assert(this.activeRoute is not null, "expecting to have an active route");

        LogSyncWithRouter(this.logger);

        // Build a changeset to manipulate the URL tree for our active route
        var changes = new List<RouteChangeItem>();

        // Delete closed docks
        foreach (var dockableRoute in this.activeRoute.Children)
        {
            if (Dockable.FromId(dockableRoute.Outlet) == null)
            {
                changes.Add(
                    new RouteChangeItem()
                    {
                        ChangeAction = RouteChangeAction.Delete,
                        Outlet = dockableRoute.Outlet,
                    });
            }
        }

        // Add and Update dockables from the managed dockables collection
        foreach (var dockable in Dockable.All)
        {
            var dock = dockable.Owner;
            Debug.Assert(
                dock is not null,
                $"expecting a docked dockable, but dockable with id=`{dockable.Id}` has a null owner");

            var childRoute
                = this.activeRoute.Children.FirstOrDefault(r => dockable.Id.Equals(r.Outlet, StringComparison.Ordinal));
            if (childRoute is null)
            {
                Debug.Assert(
                    dockable.ViewModel is not null,
                    $"expecting a dockable to have a ViewModel, but dockable with id=`{dockable.Id}` does not");

                changes.Add(
                    new RouteChangeItem()
                    {
                        ChangeAction = RouteChangeAction.Add,
                        Outlet = dockable.Id,
                        ViewModelType = dockable.ViewModel?.GetType(),
                        Parameters = Parameters(dock),
                    });
            }
            else
            {
                var parameters = Parameters(dock, childRoute.Params);
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

        this.router.Navigate(
            changes,
            new PartialNavigation()
            {
                RelativeTo = this.activeRoute,
                AdditionalInfo = new AdditionalInfo(layoutChangeReason != LayoutChangeReason.Resize),
            });
    }

    private record struct AdditionalInfo(bool RebuildLayout);
}