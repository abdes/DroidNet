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

    private static AnchorPosition GetAnchorFromParams(IReadOnlyDictionary<string, string?> parameters)
    {
        /*
         * TODO(abdes): add support for relative docking
         * TODO(abdes): replace strings by directly using enum values from the AnchorPosition enum
         */

        foreach (var anchor in Enum.GetNames<AnchorPosition>())
        {
            if (!parameters.ContainsKey(anchor.ToLowerInvariant()))
            {
                continue;
            }

            // Check that no other anchor position is specified in the parameters
            foreach (var other in Enum.GetNames<AnchorPosition>().Where(n => n != "left"))
            {
                if (parameters.ContainsKey(other))
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
                        Action = RouteAction.Delete,
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
                        Action = RouteAction.Add,
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
                            Action = RouteAction.Update,
                            Outlet = dockable.Id,
                            Parameters = parameters,
                        });
                }
            }
        }

        this.router.Navigate(changes, new NavigationOptions() { RelativeTo = this.ActiveRoute });
        return;

        static Dictionary<string, string?>? Parameters(
            IDock dock,
            IReadOnlyDictionary<string, string?>? currentParameters = null)
        {
            var changed = false;
            Dictionary<string, string?> parameters = [];

            // anything other than Minimized or Pinned is a transient state
            // that should not be propagated to the router. We assume that
            // at some pint in time it will be pinned or minimized, but for
            // now, we will just consider it pinned.
            if (dock.State == DockingState.Minimized)
            {
                // TODO: refactor parameter parsing for the docker
                if (currentParameters != null)
                {
                    if (!currentParameters.ContainsKey("minimized"))
                    {
                        changed = true;
                    }
                    else
                    {
                        var current = currentParameters["minimized"];
                        if (current != null && !bool.Parse(current))
                        {
                            changed = true;
                        }
                    }
                }

                parameters["minimized"] = null;
            }

            if (dock.Anchor != null)
            {
                var position = dock.Anchor.Position.ToString().ToLowerInvariant();
                var relativeTo = dock.Anchor.DockId?.ToString();

                if (currentParameters != null)
                {
                    if (!currentParameters.ContainsKey(position))
                    {
                        changed = true;
                    }
                    else
                    {
                        if (currentParameters[position] != relativeTo)
                        {
                            changed = true;
                        }
                    }
                }

                parameters[position] = relativeTo;
            }

            if (currentParameters == null)
            {
                changed = true;
            }

            return changed ? parameters : null;
        }
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

            // TODO(abdes): make the Params map case-insensitive
            var dockerActivatedRoute = this.ActiveRoute.Children.First(c => c.Outlet == dockableId);
            var dockingPosition = GetAnchorFromParams(dockerActivatedRoute.Params);

            var isMinimized = false;
            if (dockerActivatedRoute.Params.TryGetValue("minimized", out var minimized))
            {
                isMinimized = minimized is null || bool.Parse(minimized);
            }

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

        /*
                Debug.Assert(
                    this.ActiveRoute is not null,
                    $"when `{nameof(this.LoadContent)}`() is called, an {nameof(IActiveRoute)} should have been injected into my `{nameof(this.ActiveRoute)}` property.");

                // TODO(abdes): make the Params map case-insensitive

                var dockerActivatedRoute = this.ActiveRoute.Children.First(c => c.Outlet == "dock");
                var dockingPosition = dockerActivatedRoute.Params["position"] ??
                                      throw new InvalidOperationException("must specify a docking position");

                var dockableActivatedRoute = dockerActivatedRoute.Children.First(c => c.Outlet == dockableId);
                var isPinned = false;
                if (dockableActivatedRoute.Params.TryGetValue("pinned", out var pinned))
                {
                    isPinned = pinned is null || bool.Parse(pinned);
                }

                var dockable = this.docking.CreateDockable(dockableId, viewModel);
                var dock = this.docking.CreateDock();
                this.dockers[dockingPosition]
                    .AddDockable(dockable, dock, isPinned ? DockingState.Pinned : DockingState.Minimized);
        */
    }
}
