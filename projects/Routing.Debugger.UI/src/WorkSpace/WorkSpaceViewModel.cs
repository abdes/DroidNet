// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Utils;
using DroidNet.Routing.Events;
using DroidNet.Routing.View;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>The ViewModel for the docking workspace.</summary>
public partial class WorkSpaceViewModel : ObservableObject, IOutletContainer, IRoutingAware, IDisposable
{
    private readonly ILogger logger;
    private readonly Docker docker = new();
    private readonly IDisposable routerEventsSub;
    private readonly List<RoutedDockable> deferredDockables = [];
    private ApplicationDock? centerDock;

    public WorkSpaceViewModel(IRouter router, IViewLocator viewLocator, ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");
        this.Layout = new WorkSpaceLayout(router, this.docker, viewLocator, this.logger);

        this.routerEventsSub = router.Events.OfType<ActivationComplete>()
            .Subscribe(@event => this.PlaceDocks(@event.Options));
    }

    public IActiveRoute? ActiveRoute { get; set; }

    public WorkSpaceLayout Layout { get; }

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
    }

    public void Dispose()
    {
        this.Layout.Dispose();
        this.docker.Dispose();
        this.routerEventsSub.Dispose();
        GC.SuppressFinalize(this);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Application outlet populated with ViewModel: {Viewmodel}")]
    private static partial void LogAppLoaded(ILogger logger, object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Dockable outlet `{Outlet}` populated with ViewModel: {Viewmodel}")]
    private static partial void LogDockableLoaded(ILogger logger, OutletName outlet, object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Dockable with ID `{DockableId}` trying to dock relative to unknown ID `{RelativeToId}`")]
    private static partial void LogInvalidRelativeDocking(ILogger logger, string dockableId, string relativeToId);

    private void LoadApp(object viewModel)
    {
        this.centerDock = ApplicationDock.New() ?? throw new ContentLoadingException(
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
        this.centerDock.AddDockable(dockable);

        this.docker.DockToCenter(this.centerDock);

        LogAppLoaded(this.logger, viewModel);
    }

    private void LoadDockable(object viewModel, string dockableId)
    {
        Debug.Assert(
            this.ActiveRoute is not null,
            $"when `{nameof(this.LoadContent)}`() is called, an {nameof(IActiveRoute)} should have been injected into my `{nameof(this.ActiveRoute)}` property.");

        try
        {
            /*
             * From the dockable corresponding ActiveRoute, we get the parameters and reconstruct the dockable. Docking
             * will be deferred, as we can only do the docking once all dockables have been loaded.
             */

            var dockableActiveRoute = this.ActiveRoute.Children.First(c => c.Outlet == dockableId);
            var dockable = RoutedDockable.New(dockableId, dockableActiveRoute);

            this.deferredDockables.Add(dockable);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException(dockableId, viewModel, ex.Message, ex);
        }
    }

    private void PlaceDocks(NavigationOptions options)
    {
        // Called when activation is complete. If the navigation is partial, the
        // docking tree is already built. Only changes will be applied.
        // if the navigation mode is Full, all dockables and docks are created
        // but no docks have been created yet and no docking took place.
        if (options is not FullNavigation)
        {
            return;
        }

        // Sort the dockables collection to put root-anchored docks first. This
        // guarantees that all, docks are created before we start encountering
        // any relative docking cases.
        this.deferredDockables.Sort(
            (left, right)
                => left.DeferredDockingInfo.AnchorId == null && right.DeferredDockingInfo.AnchorId == null ? 0 :
                left.DeferredDockingInfo.AnchorId == null ? -1 : 1);

        try
        {
            foreach (var dockable in this.deferredDockables)
            {
                try
                {
                    var dockingInfo = dockable.DeferredDockingInfo;

                    if (dockingInfo.AnchorId == null)
                    {
                        // Dock to root
                        var dock = ToolDock.New();
                        dock.AddDockable(dockable);
                        this.docker.DockToRoot(dock, dockingInfo.Position, dockingInfo.IsMinimized);
                    }
                    else
                    {
                        // Find the relative dock, it must have been already
                        // created above, or the ID used for the anchor is
                        // invalid.
                        var relativeDockable = Dockable.FromId(dockingInfo.AnchorId);
                        if (relativeDockable == null)
                        {
                            // Log an error
                            LogInvalidRelativeDocking(this.logger, dockable.Id, dockingInfo.AnchorId);

                            // Dock to root as left, minimized.
                            var dock = ToolDock.New();
                            dock.AddDockable(dockable);
                            this.docker.DockToRoot(dock, AnchorPosition.Left, true);
                        }
                        else
                        {
                            if (dockingInfo.Position == AnchorPosition.Center)
                            {
                                this.centerDock?.AddDockable(dockable);
                            }
                            else if (dockingInfo.Position == AnchorPosition.With)
                            {
                                var dock = relativeDockable.Owner;
                                Debug.Assert(
                                    dock is not null,
                                    "the dockable should have been added to the dock when it was created");
                                dock.AddDockable(dockable);
                                if (dockingInfo.IsMinimized)
                                {
                                    this.docker.MinimizeDock(dock);
                                }
                            }
                            else
                            {
                                var anchor = new Anchor(dockingInfo.Position, relativeDockable);
                                var dock = ToolDock.New();
                                dock.AddDockable(dockable);
                                this.docker.Dock(dock, anchor, dockingInfo.IsMinimized);
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    // TODO: be more resilient to error, Log an error, but continue loading the other docks.
                    throw new ContentLoadingException(dockable.Id, dockable.ViewModel, ex.Message, ex);
                }

                LogDockableLoaded(this.logger, dockable.Id, dockable.ViewModel!);
            }
        }
        finally
        {
            this.deferredDockables.Clear();

            this.docker.Root.DumpGroup();
        }
    }
}
