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
using DroidNet.Routing.View;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>The ViewModel for the docking workspace.</summary>
public partial class WorkSpaceViewModel : ObservableObject, IOutletContainer, IRoutingAware, IDisposable
{
    private readonly ILogger logger;
    private readonly Docker docker = new();
    private readonly IDisposable routerEventsSub;
    private readonly List<DockingInfo> dockingInfo = [];
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

        DumpGroup(this.docker.Root);
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

    private static (AnchorPosition anchorPosition, string? anchorId) GetAnchorFromParams(IParameters parameters)
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

            var anchorPosition = Enum.Parse<AnchorPosition>(anchor);
            _ = parameters.TryGetValue(anchor, out var relativeDockableId);
            return (anchorPosition, relativeDockableId);
        }

        // return default: left
        return (AnchorPosition.Left, null);
    }

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
            var dockable = Dockable.New(dockableId) ?? throw new ObjectCreationException(typeof(Dockable));

            dockable.ViewModel = viewModel;

            var dockableActiveRoute = this.ActiveRoute.Children.First(c => c.Outlet == dockableId);

            if (dockableActiveRoute.Params.TryGetValue("w", out var preferredWidth))
            {
                dockable.PreferredWidth = new IDockable.Width(preferredWidth);
            }

            if (dockableActiveRoute.Params.TryGetValue("h", out var preferredHeight))
            {
                dockable.PreferredHeight = new IDockable.Height(preferredHeight);
            }

            /*
             From the dockable corresponding ActiveRoute, we get the parameters
             and reconstruct the dockable. Docking will be deferred though, as
             we can only do the docking once all dockables have been loaded.
            */

            var (anchorPosition, anchorId) = GetAnchorFromParams(dockableActiveRoute.Params);
            if (anchorPosition == AnchorPosition.With && anchorId is null)
            {
                throw new InvalidOperationException("you must specify the relative dockable ID when you use 'with'");
            }

            var isMinimized = dockableActiveRoute.Params.FlagIsSet("minimized");

            this.dockingInfo.Add(
                new DockingInfo()
                {
                    Dockable = dockable,
                    Position = anchorPosition,
                    RelativeToId = anchorId,
                    IsMinimized = isMinimized,
                });
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
        if (options is not FullNavigation)
        {
            return;
        }

        // if the navigation mode is Full, all dockables and docks are created
        // but no docks have been created yet and no docking took place.
        try
        {
            // Sort the dockable info collection with root docking coming first,
            // so that all docks are created before we start encountering any
            // relative docking cases.
            this.dockingInfo.Sort(
                (left, right) => left.RelativeToId == null && right.RelativeToId == null ? 0 :
                    left.RelativeToId == null ? -1 : 1);

            foreach (var dockableInfo in this.dockingInfo)
            {
                try
                {
                    if (dockableInfo.RelativeToId == null)
                    {
                        // Dock to root
                        var dock = ToolDock.New() ?? throw new ObjectCreationException(typeof(ToolDock));
                        dock.AddDockable(dockableInfo.Dockable);
                        this.docker.DockToRoot(dock, dockableInfo.Position, dockableInfo.IsMinimized);
                    }
                    else
                    {
                        // Find the relative dock, it must have been already
                        // created above, or the ID used for the anchor is
                        // invalid.
                        var relativeDockable = Dockable.FromId(dockableInfo.RelativeToId);
                        if (relativeDockable == null)
                        {
                            // Log an error
                            LogInvalidRelativeDocking(this.logger, dockableInfo.Dockable.Id, dockableInfo.RelativeToId);

                            // Dock to root as left, minimized.
                            var dock = ToolDock.New() ?? throw new ObjectCreationException(typeof(ToolDock));
                            dock.AddDockable(dockableInfo.Dockable);
                            this.docker.DockToRoot(dock, AnchorPosition.Left, true);
                        }
                        else
                        {
                            if (dockableInfo.Position == AnchorPosition.Center)
                            {
                                this.centerDock?.AddDockable(dockableInfo.Dockable);
                            }
                            else if (dockableInfo.Position == AnchorPosition.With)
                            {
                                var dock = relativeDockable.Owner;
                                Debug.Assert(
                                    dock is not null,
                                    "the dockable should have been added to the dock when it was created");
                                dock.AddDockable(dockableInfo.Dockable);
                                if (dockableInfo.IsMinimized)
                                {
                                    this.docker.MinimizeDock(dock);
                                }
                            }
                            else
                            {
                                var dock = ToolDock.New() ?? throw new ObjectCreationException(typeof(ToolDock));
                                dock.AddDockable(dockableInfo.Dockable);
                                this.docker.Dock(
                                    dock,
                                    new Anchor(dockableInfo.Position, relativeDockable),
                                    dockableInfo.IsMinimized);
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    // TODO: be more resilient to error, Log an error, but continue loading the other docks.
                    throw new ContentLoadingException(
                        dockableInfo.Dockable.Id,
                        dockableInfo.Dockable.ViewModel,
                        ex.Message,
                        ex);
                }

                LogDockableLoaded(this.logger, dockableInfo.Dockable.Id, dockableInfo.Dockable.ViewModel!);
            }
        }
        finally
        {
            this.dockingInfo.Clear();
        }
    }

    private readonly record struct DockingInfo
    {
        public required Dockable Dockable { get; init; }

        public required AnchorPosition Position { get; init; }

        public string? RelativeToId { get; init; }

        public bool IsMinimized { get; init; }
    }

    // TODO(abdes): make all application exceptions derive from ApplicationException
    private sealed class ObjectCreationException(Type type)
        : ApplicationException($"could not create an instance of {type.FullName}");
}
