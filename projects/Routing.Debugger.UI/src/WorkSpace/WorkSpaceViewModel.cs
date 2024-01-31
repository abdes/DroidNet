// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Routing.Contracts;
using DroidNet.Routing.UI.Contracts;

public class WorkSpaceViewModel : ObservableObject, IOutletContainer, IRoutingAware
{
    private readonly Docker docker;

    public WorkSpaceViewModel()
    {
        this.docker = new Docker();
        this.Root = this.docker.Root;

        //this.Docker.DockToRoot(CreateToolDockOrFail());
        //this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        //this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        //this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        //this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Bottom);
        //DumpGroup(this.Docker.Root);
    }

    public IDockGroup Root { get; }

    public IActiveRoute? ActiveRoute { get; set; }

    public void LoadContent(object viewModel, string? outletName = null)
    {
        switch (outletName)
        {
            case null:
            case Router.Outlet.Primary:
                throw new InvalidOperationException(
                    $"illegal outlet name {outletName} used for a dockable; cannot be null or `{Router.Outlet.Primary}`.");

            case DebuggerConstants.AppOutletName:
                this.LoadApp(viewModel);
                break;

            default:
                // Any other name is interpreted as the dockable ID
                this.LoadDockable(viewModel, outletName);
                break;
        }

        DumpGroup(this.docker.Root);
    }

    public object? GetViewModelForOutlet(string? outletName) => throw new NotImplementedException();

    public string GetPropertyNameForOutlet(string? outletName) => throw new NotImplementedException();

    private static ToolDock CreateToolDockOrFail()
        => ToolDock.New() ?? throw new Exception("could not create dock");

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

    private void LoadApp(object viewModel)
    {
        var dock = SingleItemDock.New() ?? throw new ContentLoadingException(
            DebuggerConstants.AppOutletName,
            viewModel,
            "could not create a dock");

        // Dock at the center
        this.docker.DockToRoot(dock);
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

            // TODO(abdes): add support for relative docking
            this.docker.DockToRoot(dock, dockingPosition);
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
