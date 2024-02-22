// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Docking.Detail;
using DroidNet.Routing.View;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>The ViewModel for the docking workspace.</summary>
public partial class WorkSpaceViewModel : ObservableObject, IOutletContainer, IRoutingAware, IDisposable
{
    private readonly ILogger logger;
    private readonly Docker docker = new();

    public WorkSpaceViewModel(IRouter router, IViewLocator viewLocator, ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");
        this.Layout = new WorkSpaceLayout(router, this.docker, viewLocator, this.logger);
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

    private static AnchorPosition GetAnchorFromParams(IParameters parameters)
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

            return Enum.Parse<AnchorPosition>(anchor);
        }

        // return default: left
        return AnchorPosition.Left;
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

        this.docker.DockToCenter(dock);

        LogAppLoaded(this.logger, viewModel);
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

            var dockerActivatedRoute = this.ActiveRoute.Children.First(c => c.Outlet == dockableId);
            var dockingPosition = GetAnchorFromParams(dockerActivatedRoute.Params);

            var isMinimized = dockerActivatedRoute.Params.FlagIsSet("minimized");

            // TODO(abdes): add support for relative docking
            // TODO(abdes): avoid explicitly creating Dockable instances
            var dockable = Dockable.New(dockableId) ??
                           throw new ContentLoadingException(
                               dockableId,
                               viewModel,
                               "failed to create a dockable object");

            if (dockerActivatedRoute.Params.TryGetValue("w", out var preferredWidth))
            {
                dockable.PreferredWidth = new IDockable.Width(preferredWidth);
            }

            if (dockerActivatedRoute.Params.TryGetValue("h", out var preferredHeight))
            {
                dockable.PreferredHeight = new IDockable.Height(preferredHeight);
            }

            dockable.ViewModel = viewModel;
            dock.AddDockable(dockable);
            this.docker.DockToRoot(dock, dockingPosition, isMinimized);

            LogDockableLoaded(this.logger, dockableId, viewModel);
        }
        catch (Exception ex)
        {
            throw new ContentLoadingException(dockableId, viewModel, ex.Message, ex);
        }
    }
}
