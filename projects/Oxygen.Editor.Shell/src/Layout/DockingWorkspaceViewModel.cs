// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Disposables;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Composition;
using DroidNet.Docking;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>
///     Base ViewModel for managing the docking workspace.
/// </summary>
/// <param name="router">
///     The parent router, will only be used for navigation out of the workspace. A local router will be created and used for local navigation inside the workspace.
/// </param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition process. If <see langword="null"/>, logging is disabled.
/// </param>
public abstract partial class DockingWorkspaceViewModel(
    IRouter router,
    ILoggerFactory? loggerFactory = null)
    : ObservableObject, IRoutingAware, IOutletContainer, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger("WorkSpace") ?? NullLoggerFactory.Instance.CreateLogger("WorkSpace");

    private readonly List<RoutedDockable> deferredDockables = [];

    private ScopeHandle? scope;
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
    ///     Gets the commands of the workspace's window, which its frame binds to keys, once the workspace is set up.
    /// </summary>
    public ICommandService? Commands { get; private set; }

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
    public virtual async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // The window's id is known only when the workspace is the content of a window.
        var window = navigationContext.NavigationTarget as Window;
        var context = new LocalRouterContext(navigationContext.NavigationTarget)
        {
            ParentRouter = router,
            RootViewModel = this.ThisViewModel,
        };
        this.scope = this.CreateWindowScope(
            [new RouterScopeModule(this.RoutesConfig, context), new DockingWindowModule(), .. this.CreateWindowModules()],
            window is null ? null : registrations => registrations.RegisterInstance(window.AppWindow.Id));

        var resolver = this.scope.Resolver;
        this.Docker = resolver.Resolve<IDocker>();
        this.Layout = resolver.Resolve<DockingWorkspaceLayout>();
        this.Commands = resolver.Resolve<ICommandService>(IfUnresolved.ReturnDefault);
        this.OnWindowScopeOpened(resolver);

        var localRouter = resolver.Resolve<IRouter>();
        this.localRouterEventsSub = new CompositeDisposable(
            localRouter.Events.OfType<ActivationComplete>()
                .Subscribe(
                    _ => this.PlaceDocks()),
            localRouter.Events.OfType<ActivationStarted>()
                .Subscribe(
                    @event =>
                    {
                        this.activeRoute = @event.Context.State?.RootNode ?? throw new InvalidOperationException("bad local router state");
                        this.Layout.ActiveRoute = this.activeRoute;
                    }));

        await this.OnInitialNavigationAsync(context).ConfigureAwait(true);
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
                this.Docker?.FindDockable(this.CenterOutletName)?.Dispose();
                this.centerDock.Dispose();
            }

            // Release all dockables that were created for this workspace
            foreach (var dockable in this.deferredDockables)
            {
                dockable.Dispose();
            }

            this.deferredDockables.Clear();
            this.localRouterEventsSub?.Dispose();

            // The window's scope belongs to its owner, which disposes it, awaited, when the window's session closes.
            this.scope = null;
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
    /// Creates the workspace window's scope from its modules, under the scope that owns the window and disposes its
    /// scope.
    /// </summary>
    /// <param name="modules">The window's modules, in registration and activation order.</param>
    /// <param name="hostInstances">Registers the window's own instances, such as its id.</param>
    /// <returns>The window's scope.</returns>
    protected abstract ScopeHandle CreateWindowScope(IReadOnlyList<IScopeModule<IWindowLayer>> modules, Action<IRegistrator>? hostInstances);

    /// <summary>
    /// Creates the modules of the workspace window's scope, which register after its router and its docking.
    /// </summary>
    /// <returns>The window's modules, in registration and activation order.</returns>
    protected abstract IEnumerable<IScopeModule<IWindowLayer>> CreateWindowModules();

    /// <summary>
    /// Called once the workspace window's scope is open, with every module registered and activated.
    /// </summary>
    /// <param name="window">The resolver of the window's scope.</param>
    protected abstract void OnWindowScopeOpened(IResolver window);

    private void LoadCenterDock(object viewModel)
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        try
        {
            this.centerDock = this.Docker.CreateDock<CenterDock>();
            var dockable = this.Docker.CreateDockable<Dockable>(this.CenterOutletName);
            dockable.ViewModel = viewModel;
            this.centerDock.AdoptDockable(dockable);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException($"could not create the center dock and its dockable to load content for route `{this.activeRoute!.Config.Path}`", ex)
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
            var dockable = this.Docker.CreateDockable<RoutedDockable>(dockableId, dockableActiveRoute);
            if (this.scope?.Resolver.Resolve<ContributionRegistry>(IfUnresolved.ReturnDefault)?.TitleOf(dockableId) is { } title)
            {
                dockable.Title = title;
            }

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
    private void PlaceDocks()
    {
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        // Called when activation is complete. The dockables the navigation's activated routes loaded are created but
        // not docked: all of them for a full navigation, which builds the docking tree, and those a partial
        // navigation added, such as a closed panel shown again, which dock into the tree already built.
        if (this.deferredDockables.Count == 0)
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
        Debug.Assert(this.Docker is not null, "docker should be setup by now");

        var dockingInfo = dockable.DeferredDockingInfo;

        if (dockingInfo.AnchorId == null)
        {
            this.DockToRoot(dockable, dockingInfo);
            return;
        }

        // Find the relative dock, it must have been already created above, or the ID used for the
        // anchor is invalid.
        var relativeDockable = this.Docker.FindDockable(dockingInfo.AnchorId);
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

        var dock = this.Docker.CreateDock<ToolDock>();
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
                var dock = this.Docker.CreateDock<ToolDock>();
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
}
