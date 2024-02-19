// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Routing.Events;

public class WorkSpaceViewModel : ObservableObject, IOutletContainer, IRoutingAware, IDisposable
{
    private readonly IRouter router;
    private readonly IDisposable navigationSub;

    public WorkSpaceViewModel(IRouter router)
    {
        this.router = router;

        this.Docker = new Docker();

        this.navigationSub = this.router.Events.Where(e => e.GetType().IsAssignableTo(typeof(NavigationEvent)))
            .Subscribe(
                x =>
                {
                    switch (x)
                    {
                        case NavigationStart:
                            this.Docker.LayoutChanged -= this.SyncWithRouter;
                            break;

                        case NavigationEnd:
                        case NavigationError:
                            this.Docker.LayoutChanged += this.SyncWithRouter;
                            break;

                        default:
                            throw new ArgumentException($"unexpected event type {x.GetType()}");
                    }
                });

        this.Root = this.Docker.Root;
    }

    public IDockGroup Root { get; }

    public IActiveRoute? ActiveRoute { get; set; }

    public IDocker Docker { get; }

    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (outletName is null || outletName.IsPrimary)
        {
            throw new InvalidOperationException(
                $"illegal outlet name {outletName} used for a dockable; cannot be null or `{OutletName.Primary}`.");
        }

        if (outletName == DebuggerConstants.AppOutletName)
        {
            this.LoadApp(viewModel);
        }
        else
        {
            // Any other name is interpreted as the dockable ID
            this.LoadDockable(viewModel, outletName);
        }

        DumpGroup(this.Docker.Root);
    }

    public void Dispose()
    {
        this.navigationSub.Dispose();
        this.Docker.Dispose();
        GC.SuppressFinalize(this);
    }

    private static void DumpGroup(IDockGroup group, string indent = "")
    {
        Debug.WriteLine($"{indent}{group}");
        if (group.First is not null)
        {
            DumpGroup(group.First, indent + "   ");
        }

        if (group.Second is not null)
        {
            DumpGroup(group.Second, indent + "   ");
        }
    }

    private static AnchorPosition GetAnchorFromParams(IParameters parameters)
    {
        /*
         * TODO(abdes): add support for relative docking
         * TODO(abdes): replace strings by directly using enum values from the AnchorPosition enum
         */

        foreach (var anchor in Enum.GetNames<AnchorPosition>())
        {
            if (!parameters.Contains(anchor))
            {
                continue;
            }

            // Check that no other anchor position is specified in the parameters
            foreach (var other in Enum.GetNames<AnchorPosition>().Where(n => n != anchor))
            {
                if (parameters.Contains(other))
                {
                    throw new InvalidOperationException(
                        $"you can only specify an anchor position for a dockable once. We first found `{anchor}`, then `{other}`");
                }
            }

            return Enum.Parse<AnchorPosition>(anchor);
        }

        // return default: left
        return AnchorPosition.Left;
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

        return changed ? nextParams : null;
    }

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

    private void SyncWithRouter()
    {
        Debug.Assert(this.ActiveRoute is not null, "expecting to have an active route");

        // Build a changeset to manipulate the URL tree for our active route
        var changes = new List<RouteChangeItem>();

        // Delete closed docks
        foreach (var dockableRoute in this.ActiveRoute.Children)
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
                = this.ActiveRoute.Children.FirstOrDefault(r => dockable.Id.Equals(r.Outlet, StringComparison.Ordinal));
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

        this.router.Navigate(changes, new NavigationOptions() { RelativeTo = this.ActiveRoute });
    }

    private void LoadApp(object viewModel)
    {
        var dock = ApplicationDock.New() ?? throw new ContentLoadingException(
            DebuggerConstants.AppOutletName,
            viewModel,
            "could not create a dock");

        // Dock at the center
        var dockable = Dockable.New(DebuggerConstants.AppOutletName) ??
                       throw new ContentLoadingException(
                           DebuggerConstants.AppOutletName,
                           viewModel,
                           "failed to create a dockable object");
        dockable.ViewModel = viewModel;
        dock.AddDockable(dockable);

        this.Docker.DockToCenter(dock);
    }

    private void LoadDockable(object viewModel, string dockableId)
    {
        Debug.Assert(
            this.ActiveRoute is not null,
            $"when `{nameof(this.LoadContent)}`() is called, an {nameof(IActiveRoute)} should have been injected into my `{nameof(this.ActiveRoute)}` property.");

        try
        {
            var dock = ToolDock.New() ?? throw new ContentLoadingException(
                dockableId,
                viewModel,
                "could not create a dock");

            var dockerActivatedRoute = this.ActiveRoute.Children.First(c => c.Outlet == dockableId);
            var dockingPosition = GetAnchorFromParams(dockerActivatedRoute.Params);

            var isMinimized = dockerActivatedRoute.Params.FlagIsSet("minimized");

            // TODO(abdes): add support for relative docking
            // TODO(abdes): avoid explicitly creating Dockable instances
            var dockable = Dockable.New(dockableId) ??
                           throw new ContentLoadingException(
                               dockableId,
                               viewModel,
                               "failed to create a dockable object");
            dockable.ViewModel = viewModel;
            dock.AddDockable(dockable);
            this.Docker.DockToRoot(dock, dockingPosition, isMinimized);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException(dockableId, viewModel, ex.Message, ex);
        }
    }
}
