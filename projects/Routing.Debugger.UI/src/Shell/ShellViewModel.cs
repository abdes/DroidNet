// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Routing.Contracts;
using DroidNet.Routing.Debugger.UI;
using DroidNet.Routing.UI.Contracts;

public partial class ShellViewModel : ObservableObject, IOutletContainer, IRoutingAware
{
    private readonly Dictionary<string, IDocker> dockers;

    [ObservableProperty]
    private string? url;

    [ObservableProperty]
    private IDocker docker;

    public ShellViewModel(Router router)
    {
        // TODO(abdes): register for router events and update URL

        this.dockers
            = new Dictionary<string, IDocker>(StringComparer.OrdinalIgnoreCase)
            {
                { DebuggerConstants.LeftDocker, new Docker() },
            };

        this.Docker = this.dockers["left"];
    }

    public object? AppViewModel { get; private set; }

    public IActiveRoute? ActiveRoute { get; set; }

    public void LoadContent(object viewModel, string? outletName = null)
    {
        switch (outletName)
        {
            case null:
            case Router.Outlet.Primary:
            case DebuggerConstants.AppOutletName:
                this.LoadAppContent(viewModel);
                break;

            default:
                this.LoadDockableContent(outletName, viewModel);
                break;
        }
    }

    public object? GetViewModelForOutlet(string? outletName) => throw new NotImplementedException();

    public string GetPropertyNameForOutlet(string? outletName) => throw new NotImplementedException();

    private void LoadAppContent(object viewModel)
    {
        this.AppViewModel = viewModel;
        this.OnPropertyChanged(nameof(this.AppViewModel));
    }

    private void LoadDockableContent(string dockableId, object viewModel)
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
}
