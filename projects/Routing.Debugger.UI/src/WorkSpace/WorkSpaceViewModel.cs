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
    public WorkSpaceViewModel()
    {
        this.Docker.DockToRoot(CreateToolDockOrFail());

        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Left);
        this.Docker.DockToRoot(CreateToolDockOrFail(), AnchorPosition.Bottom);

        DumpGroup(this.Docker.Root);

        this.Root = this.Docker.Root;
    }

    public Docker Docker { get; } = new();

    public IDockGroup Root { get; }

    public IActiveRoute? ActiveRoute { get; set; }

    public void LoadContent(object viewModel, string? outletName = null) => throw new NotImplementedException();

    public object? GetViewModelForOutlet(string? outletName) => throw new NotImplementedException();

    public string GetPropertyNameForOutlet(string? outletName) => throw new NotImplementedException();

    public void LoadApp(object viewModel)
    {
    }

    public void LoadDockable(object viewModel, string dockableId)
    {
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

        //var dockable = this.docking.CreateDockable(dockableId, viewModel);
        //var dock = this.docking.CreateDock();
        //this.dockers[dockingPosition]
        //    .AddDockable(dockable, dock, isPinned ? DockingState.Pinned : DockingState.Minimized);
    }

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
}
