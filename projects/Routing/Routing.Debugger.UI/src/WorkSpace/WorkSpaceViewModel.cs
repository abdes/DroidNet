// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Docking;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Workspace;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// The ViewModel for the docking workspace.
/// </summary>
public partial class WorkSpaceViewModel : IOutletContainer, IRoutingAware, IDisposable
{
    private readonly ILogger logger;
    private readonly Docker docker = new();
    private readonly IDisposable routerEventsSub;
    private readonly List<RoutedDockable> deferredDockables = [];

    private IActiveRoute? activeRoute;
    private ApplicationDock? centerDock;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WorkSpaceViewModel"/> class.
    /// </summary>
    /// <param name="hostingContext">The hosting context for the application.</param>
    /// <param name="router">The router instance used for navigation and event handling.</param>
    /// <param name="dockViewFactory">The factory for creating dock views.</param>
    /// <param name="loggerFactory">The factory for creating loggers. If null, a default logger will be used.</param>
    public WorkSpaceViewModel(HostingContext hostingContext, IRouter router, IDockViewFactory dockViewFactory, ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");
        this.Layout = new WorkSpaceLayout(hostingContext, router, this.docker, dockViewFactory, this.logger);

        this.routerEventsSub = router.Events.OfType<ActivationComplete>()
            .Subscribe(
                @event =>
                {
                    this.Layout.ActiveRoute = this.activeRoute;
                    this.PlaceDocks(@event.Options);
                });
    }

    /// <summary>
    /// Gets the layout of the workspace.
    /// </summary>
    /// <value>
    /// The layout object that manages the docking workspace.
    /// </value>
    public WorkSpaceLayout Layout { get; }

    /// <inheritdoc/>
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (outletName?.IsPrimary != false)
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

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route)
    {
        this.activeRoute = route;
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="WorkSpaceViewModel"/> and optionally releases
    /// the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/>
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.Layout.Dispose();
            this.docker.Dispose();
            this.routerEventsSub.Dispose();
        }

        this.disposed = true;
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Application outlet populated with ViewModel: {ViewModel}")]
    private static partial void LogAppLoaded(ILogger logger, object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Dockable outlet `{Outlet}` populated with ViewModel: {ViewModel}")]
    private static partial void LogDockableLoaded(ILogger logger, OutletName outlet, object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Dockable with ID `{DockableId}` trying to dock relative to unknown ID `{RelativeToId}`")]
    private static partial void LogInvalidRelativeDocking(ILogger logger, string dockableId, string relativeToId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while loading content for route `{Path}`: {ErrorMessage}")]
    private static partial void LogDockablePlacementError(ILogger logger, string? path, string errorMessage);

    private static int PutRootDockablesFirst(RoutedDockable left, RoutedDockable right) => left.DeferredDockingInfo.AnchorId != null ? 1 : right.DeferredDockingInfo.AnchorId == null ? 0 : -1;

    private void LoadApp(object viewModel)
    {
        this.centerDock = ApplicationDock.New() ?? throw new ContentLoadingException(
            $"could not create a dock to load content for route `{this.activeRoute!.Config.Path}`")
        {
            OutletName = DebuggerConstants.AppOutletName,
            ViewModel = viewModel,
        };

        // Dock at the center
        var dockable = Dockable.New(DebuggerConstants.AppOutletName) ??
                       throw new ContentLoadingException(
                           $"could not create a dockable object while loading content for route `{this.activeRoute!.Config.Path}`")
                       {
                           OutletName = DebuggerConstants.AppOutletName,
                           ViewModel = viewModel,
                       };
        dockable.ViewModel = viewModel;
        this.centerDock.AdoptDockable(dockable);

        var anchor = new Anchor(AnchorPosition.Center);
        try
        {
            this.docker.Dock(this.centerDock, anchor);
            anchor = null; // Disposal ownership is transferred
        }
        finally
        {
            anchor?.Dispose();
        }

        LogAppLoaded(this.logger, viewModel);
    }

    private void LoadDockable(object viewModel, string dockableId)
    {
        try
        {
            /*
             * From the dockable corresponding ActiveRoute, we get the parameters and reconstruct the dockable. Docking
             * will be deferred, as we can only do the docking once all dockables have been loaded.
             */

            var dockableActiveRoute = this.activeRoute!.Children.First(c => c.Outlet == dockableId);
            var dockable = RoutedDockable.New(dockableId, dockableActiveRoute);

            this.deferredDockables.Add(dockable);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException(
                $"an exception occurred while loading content for route `{this.activeRoute!.Config.Path}`",
                ex)
            {
                OutletName = dockableId,
                ViewModel = viewModel,
            };
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "any exception is not fatal, we just continue with other docks")]
    private void PlaceDocks(NavigationOptions options)
    {
        // Called when activation is complete. If the navigation is partial, the docking tree is already built. Only
        // changes will be applied. if the navigation mode is Full, all dockables and docks are created but no docks
        // have been created yet and no docking took place.
        if (options is not FullNavigation)
        {
            return;
        }

        // Sort the dockables collection to put root-anchored docks first. This guarantees that all, docks are created
        // before we start encountering any relative docking cases.
        this.deferredDockables.Sort(PutRootDockablesFirst);

        try
        {
            foreach (var dockable in this.deferredDockables)
            {
                try
                {
                    this.PlaceDockable(dockable);
                    LogDockableLoaded(this.logger, dockable.Id, dockable.ViewModel!);
                }
                catch (Exception ex)
                {
                    // Log an error and continue with the other docks
                    LogDockablePlacementError(this.logger, this.activeRoute!.Config.Path, ex.Message);
                }
            }
        }
        finally
        {
            this.deferredDockables.Clear();
            this.docker.DumpWorkspace();
        }
    }

    private void PlaceDockable(RoutedDockable dockable)
    {
        var dockingInfo = dockable.DeferredDockingInfo;

        if (dockingInfo.AnchorId == null)
        {
            this.DockToRoot(dockable, dockingInfo);
            return;
        }

        // Find the relative dock, it must have been already created above, or the ID used for the
        // anchor is invalid.
        var relativeDockable = Dockable.FromId(dockingInfo.AnchorId);
        if (relativeDockable == null)
        {
            // Log an error
            LogInvalidRelativeDocking(this.logger, dockable.Id, dockingInfo.AnchorId);

            this.DockToRoot(dockable, dockingInfo);
            return;
        }

        this.DockRelativeTo(dockable, dockingInfo, relativeDockable);
    }

    private void DockToRoot(Dockable dockable, RoutedDockable.DockingInfo dockingInfo)
    {
        var dock = ToolDock.New();
        dock.AdoptDockable(dockable);
        var anchor = new Anchor(dockingInfo.Position);
        try
        {
            this.docker.Dock(dock, anchor, dockingInfo.IsMinimized);
            anchor = null; // Disposal ownership is transferred
        }
        finally
        {
            anchor?.Dispose();
        }
    }

    private void DockRelativeTo(Dockable dockable, RoutedDockable.DockingInfo dockingInfo, Dockable relativeDockable)
    {
        if (dockingInfo.Position == AnchorPosition.Center)
        {
            this.centerDock?.AdoptDockable(dockable);
        }
        else if (dockingInfo.Position == AnchorPosition.With)
        {
            var dock = relativeDockable.Owner;
            Debug.Assert(
                dock is not null,
                "the dockable should have been added to the dock when it was created");
            dock.AdoptDockable(dockable);
            if (dockingInfo.IsMinimized)
            {
                this.docker.MinimizeDock(dock);
            }
        }
        else
        {
            var anchor = new Anchor(dockingInfo.Position, relativeDockable);
            try
            {
                var dock = ToolDock.New();
                dock.AdoptDockable(dockable);
                this.docker.Dock(dock, anchor, dockingInfo.IsMinimized);
                anchor = null;
            }
            finally
            {
                anchor?.Dispose();
            }
        }
    }
}
