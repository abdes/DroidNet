// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Workspace;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.Routing;

namespace Oxygen.Editor.WorldEditor.Workspace;

public abstract partial class DockingWorkspaceViewModel(
    IContainer container,
    IRouter router,
    ILoggerFactory? loggerFactory = null)
    : ObservableObject, IRoutingAware, IOutletContainer, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger("WorkSpace") ?? NullLoggerFactory.Instance.CreateLogger("WorkSpace");
    private readonly Docker docker = new();
    private readonly List<RoutedDockable> deferredDockables = [];

    private bool disposed;
    private CenterDock? centerDock;
    private IDisposable? localRouterEventsSub;
    private IActiveRoute? activeRoute;

    /// <summary>
    /// Gets the layout of the workspace.
    /// </summary>
    /// <value>
    /// The layout object that manages the docking workspace.
    /// </value>
    public DockingWorkspaceLayout? Layout { get; private set; }

    protected abstract string CenterOutletName { get; }

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // Create a scoped child container for resolutions local to this content browser.
        var childContainer = container.WithRegistrationsCopy();

        var context = new LocalRouterContext(navigationContext.NavigationTarget)
        {
            ParentRouter = router,
            RootViewModel = this.ThisViewModel,
        };

        var viewLocator = new DefaultViewLocator(childContainer, loggerFactory);
        childContainer.RegisterInstance<IViewLocator>(viewLocator);
        childContainer.RegisterInstance(new ViewModelToView(viewLocator));
        childContainer.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
        childContainer.Register<DockingWorkspaceLayout>(Reuse.Singleton);
        childContainer.RegisterInstance<IDocker>(this.docker);

        // TODO: Review dispose of all routing classes - inconsitent dispose ownership
        var localRouterContextProvider = new LocalRouterContextProvider(context);
        var localRouterContextManager = new RouterContextManager(localRouterContextProvider);
        IRouter localRouter;
        try
        {
            localRouter = new Router(
               childContainer,
               this.RoutesConfig,
               new RouterStateManager(),
               localRouterContextManager,
               new LocalRouteActivator(loggerFactory),
               localRouterContextProvider,
               new DefaultUrlSerializer(new DefaultUrlParser()),
               loggerFactory);

            context.LocalRouter = localRouter;

            localRouterContextManager = null; // Disposal ownership is transferred
            localRouterContextProvider = null; // Disposal ownership is transferred
        }
        finally
        {
            localRouterContextManager?.Dispose();
            localRouterContextProvider?.Dispose();
        }

        childContainer.RegisterInstance<ILocalRouterContext>(context);
        childContainer.RegisterInstance(context.LocalRouter);

        this.OnSetupChildContainer(childContainer);

        this.Layout = childContainer.Resolve<DockingWorkspaceLayout>();
        this.localRouterEventsSub = new CompositeDisposable(
            localRouter.Events.OfType<ActivationComplete>()
                .Subscribe(@event => this.PlaceDocks(@event.Options)),
            localRouter.Events.OfType<ActivationStarted>()
                .Subscribe(@event =>
                {
                    this.activeRoute = @event.Context.State?.RootNode ?? throw new InvalidOperationException("bad local router state");
                    this.Layout.ActiveRoute = this.activeRoute;
                }));

        await this.OnInitialNavigationAsync(context).ConfigureAwait(true);
    }

    protected abstract Task OnInitialNavigationAsync(LocalRouterContext context);

    public abstract object ThisViewModel { get; }

    public abstract IRoutes RoutesConfig { get; }

    protected abstract void OnSetupChildContainer(IContainer childContainer);

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
    /// Releases the unmanaged resources used by the <see cref="WorkspaceViewModel"/> and optionally releases
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
            this.Layout?.Dispose();
            this.docker.Dispose();
            this.localRouterEventsSub?.Dispose();
        }

        this.disposed = true;
    }

    private void LoadCenterDock(object viewModel)
    {
        this.centerDock = CenterDock.New() ?? throw new ContentLoadingException(
            $"could not create a dock to load content for route `{this.activeRoute!.Config.Path}`")
        {
            OutletName = this.CenterOutletName,
            ViewModel = viewModel,
        };

        // Dock at the center
        var dockable = Dockable.New(this.CenterOutletName) ??
                       throw new ContentLoadingException($"could not create a dockable object while loading content for route `{this.activeRoute!.Config.Path}`")
                       {
                           OutletName = this.CenterOutletName,
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

        this.LogRendererLoaded(viewModel);
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
            throw new ContentLoadingException($"an exception occurred while loading content for route `{this.activeRoute!.Config.Path}`", ex)
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
            this.docker.DumpWorkspace();
        }

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
