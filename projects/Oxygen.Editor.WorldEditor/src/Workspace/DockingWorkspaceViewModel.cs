// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.Routing;

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
/// Base ViewModel for managing the docking workspace.
/// </summary>
/// <param name="container">The parent IoC container to be used to create a child container for this workspace.</param>
/// <param name="router">The parent router, will only be used for navigation out of the workspace. A local router will be created and used for local navigation inside the workspace.</param>
/// <param name="loggerFactory">Optional factory for creating loggers. If provided, enables detailed logging of the recognition process. If <see langword="null"/>, logging is disabled. </param>
public abstract partial class DockingWorkspaceViewModel(
    IContainer container,
    IRouter router,
    ILoggerFactory? loggerFactory = null)
    : ObservableObject, IRoutingAware, IOutletContainer, IDisposable
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1823:Avoid unused private fields", Justification = "logging code is source generated")]
    private readonly ILogger logger = loggerFactory?.CreateLogger("WorkSpace") ?? NullLoggerFactory.Instance.CreateLogger("WorkSpace");

    private readonly IContainer childContainer = container.CreateChild();

    private readonly List<RoutedDockable> deferredDockables = [];

    private CompositeDisposable? localRouterEventsSub;
    private bool disposed;

    private CenterDock? centerDock;
    private IActiveRoute? activeRoute;

    /// <summary>
    /// Gets the Docker instance.
    /// </summary>
    public IDocker? Docker { get; private set; }

    /// <summary>
    /// Gets the layout of the workspace.
    /// </summary>
    public DockingWorkspaceLayout? Layout { get; private set; }

    /// <summary>
    /// Gets the name of the center outlet.
    /// </summary>
    protected abstract string CenterOutletName { get; }

    /// <summary>
    /// Gets the ViewModel instance.
    /// </summary>
    protected abstract object ThisViewModel { get; }

    /// <summary>
    /// Gets the routes configuration.
    /// </summary>
    protected abstract IRoutes RoutesConfig { get; }

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        _ = this.childContainer
            .WithMvvm()
            .WithLocalRouting(
                this.RoutesConfig,
                new LocalRouterContext(navigationContext.NavigationTarget)
                {
                    ParentRouter = router,
                    RootViewModel = this.ThisViewModel,
                })
            .WithDocking();

        this.OnSetupChildContainer(this.childContainer);

        this.Docker = this.childContainer.Resolve<IDocker>();
        this.Layout = this.childContainer.Resolve<DockingWorkspaceLayout>();

        var localRouter = this.childContainer.Resolve<IRouter>();
        this.localRouterEventsSub = new CompositeDisposable(
            localRouter.Events.OfType<ActivationComplete>()
                .Subscribe(
                    @event =>
                        this.PlaceDocks(@event.Options)),
            localRouter.Events.OfType<ActivationStarted>()
                .Subscribe(
                    @event =>
                    {
                        this.activeRoute = @event.Context.State?.RootNode ?? throw new InvalidOperationException("bad local router state");
                        this.Layout.ActiveRoute = this.activeRoute;
                    }));

        await this.OnInitialNavigationAsync(this.childContainer.Resolve<ILocalRouterContext>()).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (outletName?.IsPrimary != false)
        {
            throw new InvalidOperationException($"illegal outlet name {outletName} used for a dockable; cannot be null or `{OutletName.Primary}`.");
        }

        if (outletName == this.CenterOutletName)
        {
            this.LoadCenterDock(viewModel);
        }
        else
        {
            // Any other name is interpreted as the dockable ID
            this.LoadDockable(viewModel, outletName);
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="DockingWorkspaceViewModel"/> and optionally releases
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
            // Release the center dock dockable
            if (this.centerDock is not null)
            {
                Dockable.FromId(this.CenterOutletName)?.Dispose();
                this.centerDock.Dispose();
            }

            // Release all dockables that were created for this workspace
            foreach (var dockable in this.deferredDockables)
            {
                dockable.Dispose();
            }

            this.deferredDockables.Clear();
            this.childContainer.Dispose();
            this.localRouterEventsSub?.Dispose();
        }

        this.disposed = true;
    }

    /// <summary>
    /// Called during initial navigation to perform any necessary setup.
    /// </summary>
    /// <param name="context">The local router context.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    protected abstract Task OnInitialNavigationAsync(ILocalRouterContext context);

    /// <summary>
    /// Called to set up the child container with additional registrations.
    /// </summary>
    /// <param name="childContainer">The child container to set up.</param>
    protected abstract void OnSetupChildContainer(IContainer childContainer);

    private void LoadCenterDock(object viewModel)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        try
        {
            this.centerDock = CenterDock.New();
            var dockable = Dockable.New(this.CenterOutletName);
            dockable.ViewModel = viewModel;
            this.centerDock.AdoptDockable(dockable);
        }
        catch (Exception)
        {
            throw new ContentLoadingException($"could not create the center dock and its dockable to load content for route `{this.activeRoute!.Config.Path}`")
            {
                OutletName = this.CenterOutletName,
                ViewModel = viewModel,
            };
        }

        var anchor = new Anchor(AnchorPosition.Center);
        try
        {
            this.Docker.Dock(this.centerDock, anchor);
            anchor = null; // Disposal ownership is transferred
        }
        finally
        {
            anchor?.Dispose();
        }

        this.LogRendererLoaded(viewModel);
    }

    private void LoadDockable(object viewModel, string dockableId)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        // From the dockable corresponding ActiveRoute, we get the parameters
        // and reconstruct the dockable.Docking will be deferred, as we can only
        // do the docking once all dockables have been loaded.
        try
        {
            var dockableActiveRoute = this.activeRoute!.Children.First(c => c.Outlet == dockableId);
            var dockable = RoutedDockable.New(dockableId, dockableActiveRoute);

            this.deferredDockables.Add(dockable);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException($"an exception occurred while loading content for route `{this.activeRoute!.Config.Path}`", ex)
            {
                OutletName = dockableId,
                ViewModel = viewModel,
            };
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "any exception is not fatal, we just continue with other docks")]
    private void PlaceDocks(NavigationOptions options)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

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
                    this.LogDockableLoaded(dockable.Id, dockable.ViewModel!);
                }
                catch (Exception ex)
                {
                    // Log an error and continue with the other docks
                    this.LogDockablePlacementError(this.activeRoute!.Config.Path, ex.Message);
                }
            }
        }
        finally
        {
            this.deferredDockables.Clear();
            this.Docker.DumpWorkspace();
        }

        return;

        static int PutRootDockablesFirst(RoutedDockable left, RoutedDockable right)
        {
            return left.DeferredDockingInfo.AnchorId != null ? 1 : right.DeferredDockingInfo.AnchorId == null ? 0 : -1;
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
            this.LogInvalidRelativeDocking(dockable.Id, dockingInfo.AnchorId);

            this.DockToRoot(dockable, dockingInfo);
            return;
        }

        this.DockRelativeTo(dockable, dockingInfo, relativeDockable);
    }

    private void DockToRoot(Dockable dockable, RoutedDockable.DockingInfo dockingInfo)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        var dock = ToolDock.New();
        dock.AdoptDockable(dockable);
        var anchor = new Anchor(dockingInfo.Position);
        try
        {
            this.Docker.Dock(dock, anchor, dockingInfo.IsMinimized);
            anchor = null; // Disposal ownership is transferred
        }
        finally
        {
            anchor?.Dispose();
        }
    }

    private void DockRelativeTo(Dockable dockable, RoutedDockable.DockingInfo dockingInfo, Dockable relativeDockable)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

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
                this.Docker.MinimizeDock(dock);
            }
        }
        else
        {
            var anchor = new Anchor(dockingInfo.Position, relativeDockable);
            try
            {
                var dock = ToolDock.New();
                dock.AdoptDockable(dockable);
                this.Docker.Dock(dock, anchor, dockingInfo.IsMinimized);
                anchor = null;
            }
            finally
            {
                anchor?.Dispose();
            }
        }
    }

    [LoggerMessage(
    SkipEnabledCheck = true,
    Level = LogLevel.Information,
    Message = "Renderer outlet populated with ViewModel: {ViewModel}")]
    private partial void LogRendererLoaded(object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Dockable outlet `{Outlet}` populated with ViewModel: {ViewModel}")]
    private partial void LogDockableLoaded(OutletName outlet, object viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Dockable with ID `{DockableId}` trying to dock relative to unknown ID `{RelativeToId}`")]
    private partial void LogInvalidRelativeDocking(string dockableId, string relativeToId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while loading content for route `{Path}`: {ErrorMessage}")]
    private partial void LogDockablePlacementError(string? path, string errorMessage);
}
